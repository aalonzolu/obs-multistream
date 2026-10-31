# Contrato observado: Streamlabs → TikTok LIVE

**Estado:** observado en código de terceros; **no** es una API pública ni estable. Se valida con respuestas reales en la Fase 1 (`spike/tiktok/`).

| Fuente | Commit |
|---|---|
| Loukious/StreamLabsTikTokStreamKeyGenerator (`TokenRetriever.py`, `Stream.py`, GUI) — GPL-3.0 | `ef03fb23cb1680eb47e9598850509ce22e218600` |
| Loukious/obs-multi-rtmp (`src/streamlabs-api.cpp`, `src/push-widget.cpp`) — GPL-2.0 | `4518eca86c596c549aa8d75101f1a92bc00811a4` |

No se copió código de ninguno de los dos. El spike reimplementa el protocolo a partir de este documento.

## 1. Login

```
GET https://streamlabs.com/slobs/login
    ?skip_splash=true&external=electron&tiktok&force_verify
    &origin=slobs&port={puerto}
    &code_challenge={S256}&code_flow=true
```

- PKCE: `code_verifier` = 64 bytes aleatorios en hex (128 caracteres); `code_challenge` = base64url(SHA-256(verifier)) sin relleno.
- **No hay `state` ni `redirect_uri`.** Streamlabs arma el callback a partir de `port`. El host y el path exactos no están documentados; la referencia acepta cualquier path.
- Callback esperado: `?success=true&code={code}`.

### Canje

```
GET https://streamlabs.com/api/v5/slobs/auth/data?code_verifier={v}&code={c}
→ 200 { "success": true, "data": { "oauth_token": "…" } }
```

- El verifier y el código viajan en el query string por diseño del proveedor. **Nunca registrar URLs de esta petición.**
- No se observa refresh token ni expiración declarada.

## 2. Información de cuenta y categorías

```
GET https://streamlabs.com/api/v5/slobs/tiktok/info[?category={q}]
Authorization: Bearer {oauth_token}
```

Campos usados por la referencia:

| Campo | Uso |
|---|---|
| `user.username` | Identidad visible. |
| `application_status.status` | Estado de la solicitud de acceso LIVE. |
| `can_be_live` | Habilita o bloquea el inicio. |
| `categories[].full_name`, `categories[].game_mask_id` | Selección de categoría. |

- `q` se corta a 25 caracteres; según un comentario de la referencia, más largo devuelve 500.
- **"Other" no lo devuelve la API**: la referencia lo agrega del lado del cliente con `game_mask_id: ""`. Falta confirmar que el servidor acepta `category=""`.

## 3. Inicio

```
POST https://streamlabs.com/api/v5/slobs/tiktok/stream/start
Authorization: Bearer {oauth_token}
Content-Type: multipart/form-data

title           = {título}
category        = {game_mask_id | ""}
audience_type   = "0" | "1"        (1 = contenido para adultos)
device_platform = "win32"
```

- Éxito: `{ "id": …, "rtmp": "…", "key": "…" }`.
- Error: `{ "message": "…", "data": { "message": "…" } }`.
- La URL de ingestión es `{rtmp}/{key}`, igual que el servicio personalizado de OBS.

## 4. Finalización

```
POST https://streamlabs.com/api/v5/slobs/tiktok/stream/{id}/end
Authorization: Bearer {oauth_token}
→ { "success": true }
```

## 5. Cabeceras

Ambas referencias envían el `User-Agent` de Streamlabs Desktop sobre Electron (1.17.0 en un archivo, 1.20.4 en otro). El canje también envía cabeceras `Sec-Fetch-*`. La integración depende de presentarse como Streamlabs Desktop; si eso cambia, se rompe.

## 6. Hallazgos nuevos respecto a la sección 7 del plan

| Hallazgo | Dónde | Tratamiento |
|---|---|---|
| El token de Streamlabs se guarda como `password` del servicio RTMP, en el JSON del plugin, en texto plano. | `push-widget.cpp:651-660` | Almacén seguro del sistema. |
| El C++ deriva el id de cierre del texto de la clave (`stream-<dígitos>`) en lugar del `id` de la respuesta. | `streamlabs-api.cpp:18`, `push-widget.cpp:730` | Persistir el `id` de la respuesta antes de emitir. |
| El C++ escoge la categoría por coincidencia aproximada; el Python exige nombre exacto. | `CategorySearch` / `fetch_game_mask_id` | Selección explícita por `game_mask_id`. |
| El C++ no fija timeouts; el Python usa 30 s. | — | 15 s y resultado incierto en `start`. |
| `device_platform` es siempre `win32`, incluso en macOS. | ambos | Se conserva igual hasta medir lo contrario. |

## 7. Respuestas observadas

**2026-10-31, cuenta Streamlabs Free, sin acceso LIVE** (`spike/tiktok/out/fixtures/`):

- Callback: `http://127.0.0.1:{puerto}/?code=…&success=true`. Path `/`, host `127.0.0.1`.
- `auth/data` devuelve mucho más que `oauth_token`: `token` (250 car.), `platform_token` (token de TikTok, 72 car.), `api_token.{access_token, refresh_token}` (1105 y 898 car.), `email`, `platform_id`, `first_time_user`. **El cliente solo debe conservar `oauth_token` y descartar el resto en memoria.**
- `oauth_token`: 25 caracteres, sin expiración declarada. Cabe de sobra en Credential Manager.
- `tiktok/info` sin categoría: `platform`, `info: []`, `reason: 0`, `user.username`, `application_status.{status, timestamp}`, `audience_controls_info.{disable, info_type, types[].{key, label}}`, `can_be_live`. No incluye `categories`.
- Valor observado de `application_status.status`: `never_applied`.

## 8. Incógnitas que resuelve el spike

1. ~~Host y path reales del callback.~~ Resuelto: `127.0.0.1`, `/`.
2. Duración del `oauth_token` y la respuesta cuando vence.
3. Estructura completa de `info`; valores de `application_status.status`; `can_be_live` con cuenta Free.
4. Si `start` hace visible el LIVE o notifica a seguidores antes de recibir señal.
5. Esquema de `rtmp` (rtmp o rtmps), formato de `key` y si una clave sirve para una segunda sesión.
6. Límite real del título (Restream documenta 32 bytes para su integración).
7. Si `category=""` es aceptado para contenido no gaming.
8. Si `end` es idempotente y qué pasa cuando la señal se corta sin llamar `end`.
9. Reconexión a la misma clave después de un corte de red.
10. Comportamiento con Streamlabs Free frente a Ultra.
11. ~~Longitud del token.~~ Resuelto: 25 caracteres.

## 9. Referencia oficial: Streamlabs Desktop

Streamlabs Desktop es código abierto (GPL-3.0). Se leyó, no se copió: `stream-labs/desktop@8200349501dec843ae12f709721c1de454dd9d67`, archivos `app/services/platforms/tiktok.ts`, `tiktok/api.ts` e `i18n/en-US/tiktok.json`. Confirma y amplía lo observado en Loukious.

### Acceso LIVE

- **Lo concede TikTok, no Streamlabs**, por cuenta de TikTok y para la app de Streamlabs (`GL6399433079641606942`). Formulario:
  `https://www.tiktok.com/falcon/live_g/live_access_pc_apply/intro/index.html?id=GL6399433079641606942&lang=es`
- Si te rechazan, Desktop vuelve a ofrecer la solicitud a los 30 días (`application_status.timestamp`).
- La propia app advierte: *"You must stream at least 50% gaming content continuously."*

### Interpretación de `info`

| `reason` | `application_status.status` | Estado en Desktop | Significado |
|---:|---|---|---|
| cualquiera ≠ 2 | `never_applied` | `never-applied` | Nunca solicitó acceso. |
| 1 | — | `approved` | Puede transmitir mediante Streamlabs. |
| 0 | `rejected` u otro | `denied` | Rechazado o sin aprobar. |
| 2 | — | `legacy` | TikTok le da clave propia desde LIVE Center (`livecenter.tiktok.com/producer`); la clave cambia en cada sesión. |
| -1 | — | `relog` | Hay que desvincular y volver a vincular TikTok en Streamlabs. |

### Inicio y cierre

- `category=""` es la opción *Other* oficial.
- `device_platform` es el sistema real del cliente (`getOS()`: `win32` o `darwin`), no siempre `win32`.
- La respuesta de `start` puede incluir además `chat_url`, `broadcast_id`, `channel_name`, `platform_id`, `region` y `chat_id`.
- HTTP 422 en `start` = usuario bloqueado para LIVE (`TIKTOK_USER_BANNED`).
- Códigos de error en `data.code`: `access_token_invalid`, `internal_error`, `invalid_params`, `rate_limit_exceeded`, `scope_not_authorized`, `scope_permission_missed`, `user_has_no_live_auth`, `user_already_live`.
- Un solo LIVE por cuenta: si otra instancia inicia, la primera termina; si ya hay un LIVE desde la app de TikTok, el nuevo queda bloqueado.
- El título se muestra hasta 32 caracteres.
- Existe `/api/v5/slobs/tiktok/refresh`; Desktop no lo usa en el flujo normal.

## 10. Primera emisión real (2026-10-31, Streamlabs Free, cuenta de desarrollo)

- `info`: `reason: 1`, `application_status.status: approved`, `can_be_live: true`; audiencias `0 Everyone`, `1 Adult Only`.
- `start` con `category=""`, `audience_type=0` y `device_platform=darwin` aceptado. Respuesta: `id` (string de 19 dígitos), `rtmp` (48 car., `rtmps://push-rtmp-l1-sg01.tiktokcdn.com:443/…`), `key` (182 car.), `chat_url`, `broadcast_id`, `channel_name`, `platform_id`, más `region`, `chat_id`, `stream_title`, `game_id` y `game_name` en `null`.
- **El `id` llega como string**, no como número.
- El ingreso asignado fue un nodo de Singapur (`sg01`) a pesar de transmitir desde Guatemala. Hay que medir la estabilidad en emisiones largas.
- `end` por `id` → `{ "success": true }`.

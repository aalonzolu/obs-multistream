# Notas técnicas

## Plataformas

- **macOS** (arm64, OBS 32): probado con emisiones reales a Facebook, YouTube y TikTok, simultáneas, y con una radio AzuraCast.
- **Windows** (x64): compila en GitHub Actions (`.github/workflows/build.yaml`); falta la prueba en un equipo real.

## Pruebas reales

| Prueba | Resultado |
|---|---|
| TikTok vía Streamlabs Free | Login desde el plugin, 3 min con VideoToolbox, 0 frames descartados. El LIVE aparece entre 20 y 80 s después de la primera señal, con ~3 s de retraso. |
| YouTube | En vivo confirmado por API a los ~20 s; cierre con `complete`. |
| Facebook (página) | En vivo confirmado por API a los ~20 s; al finalizar, el video puede quedar con `privacy: SELF`. |
| Tres destinos a la vez | 2 min, un solo codificador, 0 frames descartados, cierre confirmado en los tres. |
| Programar en YouTube | Directo privado creado con fecha, usado al transmitir (en vivo confirmado), cerrado al finalizar; otro programado se canceló y se borró. |
| Radio AzuraCast | MP3 96 kbps y AAC 256 kbps por PUT; la API de AzuraCast marca la entrada en vivo mientras dura y la libera al detener. |

## Pruebas automatizadas (`tests/e2e`)

Manejan OBS por obs-websocket (peticiones del vendor `obs-multistream`) contra receptores locales.

- `test_engine.py`: tres salidas con codificador compartido, parada y reinicio aislados, destino caído sin afectar a los demás.
- `test_coordinator.py`: caso normal, fallo en comprobación previa sin publicar, preparación incierta con limpieza, caída parcial, cierre pendiente con reintento, doble clic.
- `test_recovery.py`: cierre forzado de OBS, emisión pendiente detectada, inicio nuevo bloqueado y cierre posterior.
- `test_reconnect.py`: corte breve con reconexión en el mismo directo; agotados los reintentos, «Reintentar» o «Nuevo directo» solo en ese destino; directo cerrado del lado remoto.
- `test_ui.py`: «Iniciar transmisión» abre la ventana de Multistream, al aire muestra «Finalizar transmisión», pide confirmación y la retira si la sesión termina por otra vía.
- `test_radio.py`: los cinco formatos llegan con el códec y el Content-Type correctos; cambio automático a SOURCE si el servidor no acepta PUT; contraseña rechazada sin reintentos; reconexión tras un corte; radio independiente del video.
- `test_features.py`: destino RTMP propio (alta, transmisión con su clave y baja), grabación junto con la transmisión, aviso de silencio y de caída, programación anticipada (crear, usar los directos programados y cancelar).
- `capture_manual.py`: capturas del manual en modo demostración (destinos locales con nombres de ejemplo); respalda y restaura la configuración real.

## Interfaz

- **Barra de estado**: un ícono por destino con su indicador (forma y color distintos para listo, enviando, al aire, ocupado y atención), tiempo al aire, radio y engranaje. Clic abre la ventana de estado; clic derecho, acciones rápidas.
- **Ventana Transmitir**: recientes como plantillas, título con `{fecha}` y `{hora}`, descripción, destinos con sus opciones en la misma fila, radio, calidad y subida estimada.
- **Ventana de estado**: cada destino con estado, datos enviados, frames perdidos, reconexiones, mensaje y la acción que corresponde.
- **Botón de OBS**: mientras OBS no transmita por su cuenta, «Iniciar transmisión» abre la ventana; se desactiva desde el engranaje.

## Decisiones técnicas

| Tema | Decisión | Motivo |
|---|---|---|
| HTTP | libcurl en un pool de hilos | El Qt que trae OBS no incluye backend TLS. |
| Journal | JSON con escritura atómica (`QSaveFile`) | Volumen mínimo; evita empaquetar SQLite. |
| Facebook | Servicio intermedio en Cloudflare Workers + D1 | El secreto de la app de Meta no puede ir en el plugin. |
| Login de Facebook | Facebook Login for Business con `config_id` | `/me/accounts` viene vacío; las páginas se leen de `granular_scopes`. |
| YouTube | `enableAutoStart=true`, `enableAutoStop=false` | La confirmación se hace consultando `lifeCycleStatus`. |
| Detener salidas RTMP | Fuera del hilo de la interfaz | `obs_output_stop` puede quedarse esperando el hilo de conexión de libobs. |
| Radio | Salida propia con libavcodec/libavformat de OBS sobre el protocolo `http` | La salida FFmpeg de OBS escribe en el log las opciones (contraseña incluida) y el protocolo `icecast` de FFmpeg rechaza el montaje `/`, que usan AzuraCast y Liquidsoap. |
| Programar | El directo se crea con fecha y se reutiliza al transmitir; si nunca salió al aire, finalizar no lo borra | Los enlaces ya se compartieron; un intento fallido no debe romperlos. |
| Silencio | Callback de audio de la pista a 16 kHz, umbral −50 dBFS durante 20 s | Detecta micrófono silenciado o pista equivocada sin cargar el audio de la transmisión. |
| Radio: audio | `obs_add_raw_audio_callback` de la pista elegida, convertido por libobs al formato del códec | Sin video ni remuestreo propio; la radio no depende de la sesión de video. |

## Hallazgos operativos

- TikTok tarda entre 20 y 80 s en mostrar el LIVE; Multistream no lo marca como confirmado.
- OBS 32 marca los cierres forzados con archivos en `obs-studio/.sentinel/` y ofrece modo seguro al volver a abrir; en modo seguro no carga plugins.
- AzuraCast/Liquidsoap acepta PUT con `Expect: 100-continue`; si un servidor responde 400/405, el plugin pasa a SOURCE y lo recuerda.
- Meta responde «(#3) Application does not have the capability…» al programar directos si la app no tiene acceso avanzado; transmitir al momento funciona con el acceso estándar.
- FFmpeg reintenta una vez por su cuenta ante un 401; Multistream no vuelve a intentar con una contraseña rechazada.

## Desplegar tus propias credenciales

1. **YouTube**: proyecto de Google Cloud con YouTube Data API v3, cliente OAuth de escritorio y el permiso `youtube.force-ssl`. El ID y el secreto del cliente van en `local-config.cmake`.
2. **Facebook**: app de Meta con Facebook Login for Business y una configuración con `pages_show_list`, `pages_read_engagement`, `pages_manage_posts` y `publish_video`.
3. **Servicio de Facebook** (`broker/`): copiá `wrangler.example.toml` a `wrangler.toml` y completalo; creá la base con `npx wrangler d1 create obs-multistream-broker` y `npx wrangler d1 execute obs-multistream-broker --remote --file schema.sql`; cargá `META_APP_SECRET` y `CACHE_ENCRYPTION_KEY` con `npx wrangler secret put`, y publicá con `npx wrangler deploy`. Las páginas de privacidad, condiciones y eliminación de datos que pide Meta se sirven en `/privacy`, `/terms` y `/data-deletion`.
4. **TikTok**: usa la autorización de Streamlabs; la cuenta necesita acceso a LIVE.

## Pendiente

- Probar en Windows el paquete de GitHub Actions.
- Pruebas largas (60 y 120 min, con grabación local) y aceptación con un operador.
- Verificación formal de la app de Google para el permiso `youtube.force-ssl` (opcional; sin ella aparece el aviso de app no verificada).

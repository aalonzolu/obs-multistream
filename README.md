# obs-multistream

Plugin para OBS Studio que transmite a Facebook, YouTube y TikTok con un solo botón, sin pedir claves RTMP ni tokens al operador. Proyecto personal de obs-multistream.

![OBS al aire con Multistream](docs/manual/capturas/00-obs-al-aire.png)

**Estado:** funcionando en macOS con los tres destinos reales. Windows se compila en GitHub Actions y falta probarlo en el equipo de transmisión.

| Documento | Contenido |
|---|---|
| [`docs/manual/`](docs/manual/README.md) | Manual de uso con capturas. |
| [`docs/estado-implementacion.md`](docs/estado-implementacion.md) | Pruebas superadas, decisiones y pendientes. |
| [`docs/contrato-streamlabs.md`](docs/contrato-streamlabs.md) | Protocolo observado de Streamlabs → TikTok LIVE. |
| [`docs/fase1-tiktok.md`](docs/fase1-tiktok.md) | Resultados de la prueba inicial de TikTok. |

## Compilar

- macOS: `./scripts/build-macos.sh` y `./scripts/install-macos.sh` (con OBS cerrado).
- Windows: `cmake --preset windows-x64` y `cmake --build --preset windows-x64`.

Las credenciales de las apps van en `local-config.cmake` (fuera de git) o como `-D` al configurar:

```cmake
set(MULTISTREAM_GOOGLE_CLIENT_ID "...")
set(MULTISTREAM_GOOGLE_CLIENT_SECRET "...")
set(MULTISTREAM_META_BROKER_URL "https://...")
```

En GitHub Actions salen de los secretos `MULTISTREAM_GOOGLE_CLIENT_ID` y `MULTISTREAM_GOOGLE_CLIENT_SECRET` y de la variable `MULTISTREAM_META_BROKER_URL`.

## Pruebas

`tests/e2e` maneja OBS por obs-websocket contra receptores RTMP locales (mediamtx). `tests/e2e/capture_manual.py` regenera las capturas del manual en modo demostración, sin tocar cuentas reales.

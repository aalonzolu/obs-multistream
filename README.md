# Multistream para OBS

Plugin gratis y de código abierto para OBS Studio que transmite en Facebook, YouTube y TikTok al mismo tiempo, además de cualquier servidor RTMP y una radio Icecast, con un solo botón y sin copiar claves RTMP ni pagar Restream. Windows, macOS y Linux. Sitio y descargas: [multistream.lexo.dev](https://multistream.lexo.dev/).

*English:* free, open-source OBS multistream plugin. Stream to YouTube, Facebook, TikTok, any RTMP server and Icecast radio at once, with no stream keys to copy. See [multistream.lexo.dev/en](https://multistream.lexo.dev/en/).

![OBS al aire con Multistream](docs/manual/capturas/00-obs-al-aire.png)

- Un clic en «Iniciar transmisión» abre la ventana **Transmitir**: título, descripción, destinos y calidad, con plantillas de las transmisiones recientes.
- Cada destino es independiente: si uno se cae, los demás siguen; se puede reintentar en el mismo directo o crear uno nuevo solo ahí.
- **Radio** (Icecast, AzuraCast, Liquidsoap) en MP3, AAC, Opus, Vorbis o FLAC, sola o junto con el video. Reemplaza a programas como BUTT.
- **Programar con anticipación** en YouTube (y en Facebook si Meta aprobó la app) para compartir el enlace antes del evento.
- **Destinos RTMP propios** (Kick, Vimeo, un servidor propio), **grabación local** a la vez y **avisos** con sonido cuando un destino se cae o el audio queda en silencio.
- Todo vive en la barra de estado de OBS; no agrega paneles.

**Estado:** probado en macOS con las tres plataformas y una radio AzuraCast. Windows tiene instalador desde la 0.2.0 y está pendiente de probar en un equipo real. Requiere OBS Studio 33 o más reciente.

| Documento | Contenido |
|---|---|
| [Manual](docs/manual/README.md) | Uso con capturas. |
| [Notas técnicas](docs/estado-implementacion.md) | Decisiones, pruebas y cómo desplegar tus propias credenciales. |
| [Contrato Streamlabs](docs/contrato-streamlabs.md) | Protocolo observado de Streamlabs → TikTok LIVE. |

## Compilar

- macOS: `./scripts/build-macos.sh` y `./scripts/install-macos.sh` (con OBS cerrado).
- Windows: `cmake --preset windows-x64` y `cmake --build --preset windows-x64`.
- Linux: `./scripts/build-linux.sh` y `./scripts/install-linux.sh` (con OBS cerrado). Instala en `~/.config/obs-studio/plugins/obs-multistream`. Hace falta OBS Studio 33 o más reciente (no Flatpak, que no carga plugins externos), Qt 6 (base, base-private y svg), libcurl, libsimde, FFmpeg (libavcodec, libavformat y libavutil) y libsecret-1.

Las credenciales de tus apps de Google y Meta van en `local-config.cmake` (fuera de git) o como `-D` al configurar:

```cmake
set(MULTISTREAM_GOOGLE_CLIENT_ID "...")
set(MULTISTREAM_GOOGLE_CLIENT_SECRET "...")
set(MULTISTREAM_META_BROKER_URL "https://...")
```

Para compilar en GitHub Actions desde un fork, definí los secretos `MULTISTREAM_GOOGLE_CLIENT_ID` y `MULTISTREAM_GOOGLE_CLIENT_SECRET` y la variable `MULTISTREAM_META_BROKER_URL`.

## Pruebas

`tests/e2e` maneja OBS por obs-websocket contra receptores locales (mediamtx para RTMP y un receptor Icecast propio). `tests/e2e/capture_manual.py` regenera las capturas del manual en modo demostración, sin cuentas reales.

# Estado de la implementación

**Fecha:** 2026-10-31 · **Equipo de desarrollo:** macOS 26.6.2 arm64, OBS 32.1.2 · **Windows:** pendiente de compilar y probar.

## Pruebas reales superadas (macOS)

| Prueba | Resultado |
|---|---|
| TikTok vía Streamlabs Free (@cuenta_de_prueba) | Login desde el plugin, emisión de 3 min con VideoToolbox, 0 frames descartados. Visible en el celular a los 20–80 s de señal, ~3 s de retraso. |
| YouTube (canal personal, privado) | En vivo confirmado por API a los ~20 s, cierre con `complete`. |
| Facebook (página página de prueba) | En vivo confirmado por API a los ~20 s; al finalizar, el video queda con `privacy: SELF`. |
| Tres destinos a la vez | 2 min, un solo codificador, 0 frames descartados en los tres, cierre confirmado en los tres. |

## Pruebas automatizadas (`tests/e2e`, obs-websocket + mediamtx)

- `test_engine.py`: tres salidas con codificador compartido, parada y reinicio aislados, destino caído sin afectar a los demás.
- `test_coordinator.py`: caso normal, fallo en comprobación previa sin publicar, preparación incierta con limpieza, caída parcial, cierre pendiente con reintento, doble clic.
- `test_recovery.py`: cierre forzado de OBS, detección de la emisión pendiente, bloqueo de un inicio nuevo y cierre posterior.
- `test_reconnect.py`: corte breve con reconexión automática en el mismo directo; agotados los reintentos, transmisión parcial con «Reintentar» (mismo directo) o «Nuevo directo» (solo ese destino); directo cerrado del lado remoto.
- `test_ui.py`: el botón «Iniciar transmisión» de OBS abre la ventana de Multistream, cambia a «Finalizar transmisión» al aire, pide confirmación antes de cortar y la retira si la sesión termina por otra vía.
- `capture_manual.py`: regenera las capturas del manual en modo demostración (destinos locales con nombres de ejemplo) y restaura la configuración real al terminar.
- `spike/tiktok/tests`: contrato Streamlabs, redacción y PKCE.

## Interfaz

Manual con capturas en [`docs/manual/`](manual/README.md).

- **Barra de estado de OBS** (no hay panel acoplable): un ícono por destino con su indicador (forma y color distintos para listo, enviando, al aire, ocupado y atención), el tiempo al aire y un engranaje. Clic abre la ventana de estado; clic derecho, acciones rápidas del destino.
- **Ventana «Transmitir»** (con «Iniciar transmisión» de OBS, el engranaje o el atajo): recientes como plantillas, título con `{fecha}` y `{hora}`, descripción, destinos con sus opciones en la misma fila (visibilidad y declaración de contenido para niños en YouTube, privado al terminar en Facebook, aviso del título recortado en TikTok), calidad con la subida estimada. Enter transmite; el botón explica qué falta.
- **Ventana de estado**: todos los destinos con estado, datos enviados, frames perdidos, reconexiones, mensaje cuando algo falla y la acción que corresponde (conectar, reintentar, nuevo directo, abrir); aviso de emisión sin cerrar o de atención; finalizar.
- **Botón de OBS**: mientras OBS no transmita por su cuenta, «Iniciar transmisión» abre la ventana; al aire muestra «Finalizar transmisión» y pide confirmación. Se desactiva desde el engranaje.

## Decisiones que difieren del plan

| Tema | Plan | Implementado | Motivo |
|---|---|---|---|
| HTTP | Qt Network | libcurl en un pool de hilos | El Qt que trae OBS no incluye backend TLS. |
| Journal | SQLite | JSON con escritura atómica (`QSaveFile`) | Volumen mínimo; evita compilar y empaquetar SQLite en dos sistemas. |
| Servicio Facebook | Node.js | Cloudflare Worker (TypeScript) + D1, cuenta personal | Capa gratuita suficiente; sin servidor propio. |
| Login de Facebook | `scope` | Facebook Login for Business con `config_id` | Es el producto que asigna Meta a la app; `/me/accounts` viene vacío y las páginas se leen de `granular_scopes`. |
| YouTube | `enableAutoStart=false` | `enableAutoStart=true`, `enableAutoStop=false` | Menos pasos; la confirmación se hace consultando `lifeCycleStatus`. |

## Hallazgos operativos

- TikTok tarda entre 20 y 80 s en mostrar el LIVE; el panel no lo marca como confirmado.
- Tras un cierre forzado, OBS ofrece modo seguro; en ese modo no carga plugins y la recuperación ocurre en el siguiente inicio normal.
- OBS 32 agrega ocultos los paneles nuevos; el plugin lo muestra acoplado la primera vez.

## Pendiente

- Probar en Windows el paquete que genera GitHub Actions (`.github/workflows/build.yaml`).
- Google: la app está **en producción sin verificar** (aviso de "app no verificada", hasta 100 usuarios). La verificación formal del permiso sensible `youtube.force-ssl` queda opcional.
- Meta: la app está **publicada (modo activo)** con acceso estándar; solo autorizan cuentas con rol en la app (`cuenta-de-prueba` ya es tester).
- Pruebas largas (60 min y 120 min con grabación local) y aceptación con operador.

## Infraestructura

| Pieza | Dónde |
|---|---|
| Proyecto de Google | `<proyecto-google>` («Multistream OBS»), cliente de escritorio «Multistream OBS Desktop», en producción. |
| App de Meta | `<id-app-meta>` («Multistream OBS»), configuración de login `<id-config-login>`, publicada. |
| Servicio de Facebook | `https://<tu-worker>.workers.dev` (cuenta personal de Cloudflare, D1 `obs-multistream-broker`). Desplegar con `XDG_CONFIG_HOME=~/.config/obs-multistream-cf npx wrangler deploy` dentro de `broker/`. |
| Configuración de compilación | `local-config.cmake` (fuera de git): cliente de Google y URL del servicio. |

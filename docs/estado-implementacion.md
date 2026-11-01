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
- `spike/tiktok/tests`: contrato Streamlabs, redacción y PKCE.

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

- Compilar y probar en Windows (no hay minutos de GitHub Actions; opciones: repo público o runner propio).
- Agregar a `cuenta-de-prueba` como tester de la app de Meta.
- Google: pasar la app a producción para evitar la caducidad de 7 días del refresh token.
- Meta: comprobar visibilidad pública con la app en modo desarrollo y pasarla a modo Live para la página de la iglesia.
- Pruebas largas (60 min y 120 min con grabación local) y aceptación con operador.

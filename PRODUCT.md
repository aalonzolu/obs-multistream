# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop — panel acoplable (Qt Widgets) dentro de OBS Studio, en macOS y Windows. Hereda el tema, la tipografía y la densidad que OBS aplica al resto de su interfaz.

## Users

- **Operador voluntario** de la iglesia: opera cada culto o evento, no es técnico. Necesita ver con qué cuentas va a transmitir, iniciar, saber si algo falla y finalizar.
- **obs-multistream** (mantenedor): configura cuentas, resuelve problemas y a veces opera. Necesita el detalle técnico a mano, sin que estorbe al voluntario.

## Product Purpose

Transmitir desde OBS a Facebook, YouTube y TikTok con un solo botón y sin copiar claves, para dejar de pagar un servicio de multistream. Éxito: el voluntario completa una emisión real sin ayuda y sabe en todo momento qué destinos están al aire.

## Positioning

Todo ocurre dentro de OBS, con las escenas de siempre: no hay otra aplicación ni retransmisión en la nube. Cada destino es independiente; si uno cae, los otros siguen.

## Operating Context

- El panel va acoplado en la **franja inferior** de OBS, junto al Mezclador de audio y Controles: poco alto y más ancho que alto.
- Se usa en vivo, con atención dividida entre la cámara, el audio y el culto; las miradas al panel son breves.
- Durante el directo lo imprescindible es: estado de cada red, tiempo al aire, finalizar y un enlace para abrir y verificar cada directo.
- La configuración (conectar cuentas, opciones, diagnóstico) ocurre antes del evento y con calma.

## Capabilities and Constraints

- Destinos: Facebook (página), YouTube (canal), TikTok (vía Streamlabs). Selección por destino antes de iniciar.
- Estados por destino: sin conectar, listo, comprobando, preparando, conectando, enviando señal, en vivo confirmado (solo Facebook y YouTube; TikTok no lo confirma por API), reconectando, hay que reconectar, cierre pendiente, resultado incierto, necesita atención.
- TikTok muestra solo 32 caracteres del título.
- Atajos de teclado configurables desde OBS; confirmación al cerrar OBS con una sesión activa.

## Brand Commitments

- Español de Guatemala: voseo en textos de ayuda, infinitivo en botones.
- El panel debe sentirse parte de OBS (respeta su tema claro u oscuro), no un widget ajeno.

## Product Principles

1. Lo que está al aire se entiende de un vistazo.
2. Ningún estado se comunica solo con color.
3. El detalle técnico existe, pero no compite con lo esencial.
4. Ocupar solo el espacio necesario: lo que no se usa en vivo no ocupa lugar en vivo.

## Accessibility & Inclusion

Operable por teclado, foco visible, nombres accesibles en cada control y contraste suficiente en los temas claro y oscuro de OBS.

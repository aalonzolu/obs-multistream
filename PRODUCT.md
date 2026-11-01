# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop — integrado en OBS Studio (Qt Widgets) en macOS y Windows: íconos en la barra de estado de OBS y ventanas propias. Hereda el tema, la tipografía y la densidad que OBS aplica al resto de su interfaz.

## Users

- **Operador voluntario**: opera transmisiones de eventos o servicios en vivo y no es técnico. Necesita ver con qué cuentas va a transmitir, iniciar, saber si algo falla y finalizar.
- **Responsable técnico**: configura cuentas y la radio, resuelve problemas y a veces opera. Necesita el detalle técnico a mano, sin que estorbe al operador.

## Product Purpose

Transmitir desde OBS a Facebook, YouTube, TikTok y una radio Icecast con un solo botón y sin copiar claves, sin pagar un servicio de multistream ni usar otro programa para la radio. Éxito: el operador completa una emisión real sin ayuda y sabe en todo momento qué destinos están al aire.

## Positioning

Todo ocurre dentro de OBS, con las escenas de siempre: no hay otra aplicación ni retransmisión en la nube. Cada destino es independiente; si uno cae, los otros siguen. La radio puede salir sola o junto con el video.

## Operating Context

- OBS ocupa la pantalla con sus propios paneles; Multistream no agrega paneles: vive en la barra de estado y en ventanas que se abren cuando se necesitan.
- Se usa en vivo, con atención dividida entre la cámara, el audio y el evento; las miradas al estado son breves.
- Durante el directo lo imprescindible es: estado de cada destino, tiempo al aire, finalizar y un enlace para abrir y verificar cada directo.
- La configuración (conectar cuentas, radio, diagnóstico) ocurre antes del evento y con calma.

## Capabilities and Constraints

- Destinos: Facebook (página), YouTube (canal), TikTok (vía Streamlabs) y radio Icecast (MP3, AAC, Opus, Vorbis o FLAC). Selección por destino antes de iniciar.
- Estados por destino: sin conectar, listo, comprobando, preparando, conectando, enviando señal, en vivo confirmado (solo Facebook y YouTube; TikTok no lo confirma por API), reconectando, hay que reconectar, cierre pendiente, resultado incierto, necesita atención.
- TikTok muestra solo 32 caracteres del título.
- Atajos de teclado configurables desde OBS; confirmación al cerrar OBS con una sesión activa.

## Brand Commitments

- Español rioplatense/centroamericano: voseo en textos de ayuda, infinitivo en botones.
- Multistream debe sentirse parte de OBS (respeta su tema claro u oscuro), no un widget ajeno.

## Product Principles

1. Lo que está al aire se entiende de un vistazo.
2. Ningún estado se comunica solo con color.
3. El detalle técnico existe, pero no compite con lo esencial.
4. Ocupar solo el espacio necesario: lo que no se usa en vivo no ocupa lugar en vivo.

## Accessibility & Inclusion

Operable por teclado, foco visible, nombres accesibles en cada control y contraste suficiente en los temas claro y oscuro de OBS.

# obs-multistream

Plugin para OBS Studio que transmite a Facebook, YouTube y TikTok con un solo botón, sin pedir claves RTMP ni tokens al operador. Proyecto personal de obs-multistream.

**Estado:** Fase 1 (validación de TikTok mediante Streamlabs Free) superada en macOS. Siguiente: motor de tres salidas sobre libobs.

| Documento | Contenido |
|---|---|
| [`Multistream_Plan_Implementacion_2026-10-30.md`](Multistream_Plan_Implementacion_2026-10-30.md) | Plan completo: arquitectura, alcance, fases y criterios de aceptación. |
| [`docs/contrato-streamlabs.md`](docs/contrato-streamlabs.md) | Protocolo observado de Streamlabs → TikTok LIVE. |
| [`docs/fase1-tiktok.md`](docs/fase1-tiktok.md) | Resultados de la prueba de TikTok. |
| [`spike/tiktok/`](spike/tiktok/) | Herramienta descartable de la Fase 1. |

Las referencias de terceros se clonan en `references/` (fuera de git) en los commits que indica `docs/contrato-streamlabs.md`. No se incorpora su código.

function layout(title: string, content: string): string {
  return `<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${title}</title>
<body style="font-family:system-ui,sans-serif;max-width:40rem;margin:3rem auto;padding:0 1rem;line-height:1.5">
<h1>Multistream para OBS</h1>${content}
<hr><p style="font-size:.9em"><a href="/">Inicio</a> · <a href="/privacy">Privacidad</a> · <a href="/terms">Condiciones</a> · <a href="/data-deletion">Eliminación de datos</a></p>
</body></html>`;
}

function escape(text: string): string {
  return text.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

export interface Operator {
  name?: string;
  contact?: string;
  source?: string;
}

function contactLine(operator: Operator): string {
  return operator.contact ? `<p>Contacto: ${escape(operator.contact)}</p>` : "";
}

export function resultPage(message: string): string {
  return layout("Multistream", `<p>${escape(message)}</p>`);
}

export function homePage(operator: Operator): string {
  return layout(
    "Multistream para OBS",
    `<p>Multistream es un plugin personal y de código abierto para OBS Studio que transmite en vivo a Facebook, YouTube y TikTok con un solo botón, sin copiar claves de transmisión.</p>
<p>El video se envía directamente desde OBS a cada plataforma. Este servicio solo intermedia la autorización con Facebook y las operaciones de control de sus transmisiones.</p>
${operator.source ? `<p>Código fuente: <a href="${escape(operator.source)}">${escape(operator.source)}</a></p>` : ""}
${contactLine(operator)}`,
  );
}

export function privacyPage(baseUrl: string, operator: Operator): string {
  return layout(
    "Política de privacidad · Multistream",
    `<h2>Política de privacidad</h2>
<p><em>Última actualización: 1 de noviembre de 2026.</em></p>
<p>Multistream es un plugin de código abierto para OBS Studio${operator.name ? `; este servicio lo opera ${escape(operator.name)}` : ""}. Se ejecuta en el equipo de cada persona que lo instala y transmite solo a las cuentas y páginas que esa persona conecta y autoriza.</p>

<h3>Datos que se usan</h3>
<ul>
<li><strong>YouTube (Google):</strong> con el permiso <code>youtube.force-ssl</code>, el plugin usa los Servicios de la API de YouTube para leer el nombre e identificador del canal autorizado, comprobar que el canal pueda transmitir en vivo y crear, consultar, programar, finalizar o eliminar las emisiones en vivo que él mismo creó. Las llamadas van directamente del equipo a Google: este servicio no recibe datos de YouTube. No lee ni modifica otros videos, comentarios ni datos del canal.</li>
<li><strong>Facebook (Meta):</strong> con los permisos <code>pages_show_list</code>, <code>pages_read_engagement</code>, <code>pages_manage_posts</code> y <code>publish_video</code>, lista las páginas que administrás, transmite en la que elijas, consulta el estado del directo y, si lo pedís, deja el video en privado al terminar.</li>
<li><strong>TikTok:</strong> mediante la autorización de Streamlabs que la persona concede, el plugin obtiene el servidor y la clave de cada emisión y la finaliza.</li>
</ul>

<h3>Dónde se guardan</h3>
<p>Las credenciales se guardan en el almacén seguro del sistema de ese equipo (Llavero de macOS, Administrador de credenciales de Windows o el llavero de Linux). Los nombres de canal, página y cuenta se guardan en la configuración local del plugin. El audio y el video nunca pasan por este servicio.</p>
<p>Este servicio en <code>${escape(baseUrl)}</code> solo procesa la autorización de Facebook: guarda el resultado cifrado durante un máximo de 5 minutos para entregarlo al plugin y conserva cada operación de transmisión cifrada durante 24 horas para evitar duplicados.</p>

<h3>Uso limitado de datos de Google</h3>
<p>El uso que hace Multistream de la información recibida de las APIs de Google cumple la <a href="https://developers.google.com/terms/api-services-user-data-policy">Política de datos del usuario de los servicios de las APIs de Google</a>, incluidos los requisitos de uso limitado. Al usar Multistream con YouTube también se aplican las <a href="https://www.youtube.com/t/terms">Condiciones del servicio de YouTube</a> y la <a href="https://policies.google.com/privacy">Política de privacidad de Google</a>.</p>

<h3>Lo que no hacemos</h3>
<p>No se venden, alquilan ni comparten datos con terceros, no se usan para publicidad ni para entrenar modelos, y no se accede a ellos más allá de lo necesario para transmitir.</p>

<h3>Revocar y borrar</h3>
<p>Podés usar «Desconectar» en Multistream dentro de OBS para borrar la credencial local, revocar el acceso de Google en <a href="https://myaccount.google.com/permissions">myaccount.google.com/permissions</a> y el de Facebook en Configuración → Integraciones comerciales. Consultá también <a href="${escape(baseUrl)}/data-deletion">las instrucciones de eliminación</a>.</p>
${contactLine(operator)}`,
  );
}

export function termsPage(operator: Operator): string {
  return layout(
    "Condiciones del servicio · Multistream",
    `<h2>Condiciones del servicio</h2>
<p>Multistream se ofrece tal cual, sin garantías, a cada persona u organización que lo instala. Quien transmite es responsable del contenido que transmite y de cumplir las normas de Facebook, YouTube, TikTok y Streamlabs. Al conectar YouTube aceptás las <a href="https://www.youtube.com/t/terms">Condiciones del servicio de YouTube</a> y la <a href="https://policies.google.com/privacy">Política de privacidad de Google</a>.</p>
<p>El servicio puede cambiar o dejar de estar disponible. Las emisiones ya iniciadas continúan aunque este servicio no responda; en ese caso, el cierre puede requerir hacerlo desde la plataforma.</p>
${contactLine(operator)}`,
  );
}

export function dataDeletionPage(operator: Operator): string {
  return layout(
    "Eliminación de datos · Multistream",
    `<h2>Eliminación de datos</h2>
<p>El servicio no guarda datos personales de forma permanente: las autorizaciones pendientes se borran a los 5 minutos y los registros de transmisión a las 24 horas.</p>
<p>Para revocar el acceso, eliminá la app desde Facebook en Configuración → Seguridad e inicio de sesión → Integraciones comerciales, quitá el acceso de Google en <a href="https://myaccount.google.com/permissions">myaccount.google.com/permissions</a> y usá «Desconectar» en Multistream dentro de OBS para borrar la credencial guardada en tu equipo.</p>
${operator.contact ? `<p>Si necesitás ayuda, escribí a ${escape(operator.contact)}.</p>` : ""}`,
  );
}

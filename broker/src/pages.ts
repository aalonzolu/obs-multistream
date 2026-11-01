function layout(title: string, content: string): string {
  return `<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${title}</title>
<body style="font-family:system-ui,sans-serif;max-width:40rem;margin:3rem auto;padding:0 1rem;line-height:1.5">
<h1>Multistream para OBS</h1>${content}</body></html>`;
}

function escape(text: string): string {
  return text.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

export function resultPage(message: string): string {
  return layout("Multistream", `<p>${escape(message)}</p>`);
}

export function privacyPage(baseUrl: string): string {
  return layout(
    "Política de privacidad · Multistream",
    `<h2>Política de privacidad</h2>
<p>Multistream es un plugin personal de OBS Studio que transmite a una página de Facebook elegida por su administrador.</p>
<p>Este servicio solo intermedia la autorización con Facebook y las operaciones de transmisión en vivo (crear, consultar y finalizar). No recibe audio ni video.</p>
<p>Los permisos concedidos se usan exclusivamente para listar las páginas que administrás y transmitir en la que elijas. Los tokens de página se entregan al equipo del operador y se guardan en su almacén seguro; el servicio no los conserva más allá de una autorización en curso (5 minutos) o de una operación de transmisión (24 horas, cifrada).</p>
<p>No se venden ni comparten datos con terceros.</p>
<p>Para borrar tus datos, consultá <a href="${escape(baseUrl)}/data-deletion">las instrucciones de eliminación</a>.</p>`,
  );
}

export function dataDeletionPage(): string {
  return layout(
    "Eliminación de datos · Multistream",
    `<h2>Eliminación de datos</h2>
<p>El servicio no guarda datos personales de forma permanente: las autorizaciones pendientes se borran a los 5 minutos y los registros de transmisión a las 24 horas.</p>
<p>Para revocar el acceso, eliminá la app desde Facebook en Configuración → Seguridad e inicio de sesión → Integraciones comerciales, y usá «Desconectar» en el panel Multistream de OBS para borrar la credencial guardada en tu equipo.</p>`,
  );
}

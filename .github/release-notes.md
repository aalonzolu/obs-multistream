## Instalar

Cerrá OBS antes de instalar. Requiere OBS Studio 33 o más reciente (en OBS: Ayuda → Acerca de).

**Windows (x64):** bajá y abrí `obs-multistream-…-windows-x64-instalador.exe`. El instalador comprueba la versión de OBS, deja el plugin en `C:\ProgramData\obs-studio\plugins\obs-multistream` y quita copias viejas en otras carpetas. Como no está firmado, Windows puede avisar «Windows protegió tu PC»: tocá «Más información» y «Ejecutar de todas formas». Si preferís hacerlo a mano, descomprimí `obs-multistream-…-windows-x64.zip` dentro de `C:\ProgramData\obs-studio\plugins` (escribí `%ProgramData%\obs-studio\plugins` en la barra del Explorador; si la carpeta `plugins` no existe, creala) para que quede `plugins\obs-multistream\bin\64bit\obs-multistream.dll`.

**Linux (x64):** descomprimí `obs-multistream-…-linux-x64.zip` dentro de `~/.config/obs-studio/plugins` (queda `plugins/obs-multistream/bin/64bit/obs-multistream.so`). Necesitás OBS instalado desde el PPA oficial o desde los paquetes de tu distribución, no desde Flatpak, que no carga plugins externos, y un servicio de secretos (GNOME Keyring o KWallet) para guardar las cuentas.

**macOS (Apple Silicon e Intel):** descomprimí `obs-multistream-…-macos.zip` y mové `obs-multistream.plugin` a `~/Library/Application Support/obs-studio/plugins`. El paquete no está notarizado por Apple, así que hay que quitarle la cuarentena una vez:

```sh
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-multistream.plugin
```

Después abrí OBS: Multistream aparece en la barra de estado, abajo a la izquierda. Si Google o Meta no te dejan conectar una cuenta, podés [usar tus propias apps](https://multistream.lexo.dev/apps/). El [manual](https://github.com/aalonzolu/obs-multistream/blob/main/docs/manual/README.md) explica cómo conectar las cuentas y la radio.

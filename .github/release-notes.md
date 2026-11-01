## Instalar

Cerrá OBS antes de instalar. Requiere OBS Studio 31 o más reciente.

**Windows (x64):** descomprimí `obs-multistream-…-windows-x64.zip` dentro de `C:\ProgramData\obs-studio\plugins` (queda `plugins\obs-multistream\bin\64bit\obs-multistream.dll`).

**Linux (x64):** descomprimí `obs-multistream-…-linux-x64.zip` dentro de `~/.config/obs-studio/plugins` (queda `plugins/obs-multistream/bin/64bit/obs-multistream.so`). Necesitás OBS instalado desde el PPA oficial o desde los paquetes de tu distribución, no desde Flatpak, que no carga plugins externos, y un servicio de secretos (GNOME Keyring o KWallet) para guardar las cuentas.

**macOS (Apple Silicon e Intel):** descomprimí `obs-multistream-…-macos.zip` y mové `obs-multistream.plugin` a `~/Library/Application Support/obs-studio/plugins`. El paquete no está notarizado por Apple, así que hay que quitarle la cuarentena una vez:

```sh
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-multistream.plugin
```

Después abrí OBS: Multistream aparece en la barra de estado, abajo a la izquierda. El [manual](https://github.com/aalonzolu/obs-multistream/blob/main/docs/manual/README.md) explica cómo conectar las cuentas y la radio.

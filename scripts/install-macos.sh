#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if pgrep -xq OBS; then
  echo "OBS está abierto; cerralo antes de instalar." >&2
  exit 1
fi
target="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$target"
rsync -a --delete build_macos/RelWithDebInfo/obs-multistream.plugin/ "$target/obs-multistream.plugin/"
echo "Instalado en $target/obs-multistream.plugin"

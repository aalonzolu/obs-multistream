#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if pgrep -x obs >/dev/null; then
  echo "OBS está abierto; cerralo antes de instalar." >&2
  exit 1
fi
target="$HOME/.config/obs-studio/plugins/obs-multistream"
scripts/package-linux.sh "$target"
echo "Instalado en $target"

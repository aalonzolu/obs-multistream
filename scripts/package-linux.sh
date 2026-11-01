#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
destination="${1:?Uso: package-linux.sh <carpeta de destino>}"
module="build_x86_64/rundir/RelWithDebInfo/obs-multistream.so"
[[ -f "$module" ]] || { echo "Falta $module; compilá primero con el preset ubuntu-x86_64." >&2; exit 1; }
mkdir -p "$destination/bin/64bit" "$destination/data"
install -m 755 "$module" "$destination/bin/64bit/obs-multistream.so"
rsync -a --delete data/ "$destination/data/"

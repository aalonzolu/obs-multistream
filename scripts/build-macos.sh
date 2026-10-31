#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
log="build_macos/last-build.log"
mkdir -p build_macos
if cmake --build --preset macos > "$log" 2>&1; then
  echo "BUILD OK"
else
  grep -E "error:|Undefined symbols|ld: |\"_.*\", referenced from|^  \"" "$log" | grep -v "^    /Applications" | sort -u | head -40
  echo "BUILD FAILED (log completo: $log)"
  exit 1
fi

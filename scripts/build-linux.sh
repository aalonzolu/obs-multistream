#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64 --parallel

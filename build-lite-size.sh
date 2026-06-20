#!/usr/bin/env bash
set -euo pipefail
make -f Makefile.gogufw-lite clean
make -f Makefile.gogufw-lite
if command -v arm-none-eabi-size >/dev/null 2>&1; then
  echo
  echo "== firmware size =="
  arm-none-eabi-size firmware || true
fi

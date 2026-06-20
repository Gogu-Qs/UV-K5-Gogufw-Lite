#!/usr/bin/env sh
set -eu

IMAGE_NAME="uvk5-gogufw-lite-builder"
PROJECT_DIR="$(pwd)"
OUT_DIR="$PROJECT_DIR/compiled-firmware"

mkdir -p "$OUT_DIR"

docker build -t "$IMAGE_NAME" .

docker run --rm \
  -v "$OUT_DIR:/app/compiled-firmware" \
  "$IMAGE_NAME" \
  /bin/sh -lc 'make && cp -f firmware* compiled-firmware/'

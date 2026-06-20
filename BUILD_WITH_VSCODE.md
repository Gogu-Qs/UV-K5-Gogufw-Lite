# Build with VS Code

This package is prepared for macOS + VS Code + Docker Desktop.

## Requirements

1. Install and open Docker Desktop.
2. Open this project folder in VS Code.

## Build

In VS Code:

`Terminal` -> `Run Task...` -> `Build Firmware`

The firmware files will be copied to:

`compiled-firmware/`

## Clean build

Use:

`Terminal` -> `Run Task...` -> `Clean Build Firmware`

## What was fixed

The original Dockerfile used Arch Linux:

`FROM --platform=amd64 archlinux:latest`

On some Docker Desktop / Apple Silicon setups, Arch `pacman` fails with:

`error restricting syscalls via seccomp: 22`

This package now uses a Debian-based Docker image with `gcc-arm-none-eabi`, `python3-crcmod`, `make`, and `git` installed by `apt`, which avoids that pacman/seccomp failure.

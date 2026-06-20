# VS Code / Docker friendly build image for UV-K5 firmware
# Debian base avoids the Arch pacman sandbox/seccomp failure seen on Docker Desktop.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       gcc-arm-none-eabi \
       binutils-arm-none-eabi \
       libnewlib-arm-none-eabi \
       git \
       make \
       python3 \
       python3-crcmod \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Safe even when there are no submodules or .git metadata is unavailable.
RUN git submodule update --init --recursive || true

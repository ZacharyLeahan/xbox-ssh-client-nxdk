#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party
if [ ! -d third_party/libssh2/.git ]; then
  git clone --depth 1 --branch libssh2-1.11.1 https://github.com/libssh2/libssh2.git third_party/libssh2
fi
if [ ! -d third_party/mbedtls/.git ]; then
  git clone --depth 1 --branch v3.6.5 https://github.com/Mbed-TLS/mbedtls.git third_party/mbedtls
fi
# Small standalone MP3 decoder (MIT-0 / public domain choice), not SDL_mixer.
# Using a fixed upstream revision ensures repeatable builds.
mkdir -p third_party/dr_mp3
decoder_hash=7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356
if ! printf '%s  %s\n' "$decoder_hash" third_party/dr_mp3/dr_mp3.h | sha256sum -c --status; then
  curl -fL --retry 3 --connect-timeout 15 \
    https://raw.githubusercontent.com/mackron/dr_libs/f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c/dr_mp3.h \
    -o third_party/dr_mp3/dr_mp3.h.tmp
  printf '%s  %s\n' "$decoder_hash" third_party/dr_mp3/dr_mp3.h.tmp | sha256sum -c
  mv third_party/dr_mp3/dr_mp3.h.tmp third_party/dr_mp3/dr_mp3.h
fi
python3 tools/patch-libssh2-nxdk.py
printf 'Dependencies fetched and nxdk libssh2 header patched. Build with: make NXDK_DIR=/path/to/nxdk\n'

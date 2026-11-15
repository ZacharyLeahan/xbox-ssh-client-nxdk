#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Itools/audio-test-stubs -Iinclude    -Ithird_party/nxdk-audio/include src/xssh_audio.c tools/test-nxaudio-double-buffer.c -o "$test_binary"
"$test_binary"
"$test_binary" stop-timeout

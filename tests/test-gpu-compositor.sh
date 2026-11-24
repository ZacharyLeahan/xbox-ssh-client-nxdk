#!/usr/bin/env bash
# Standalone HOST test of XSSH tile checksums/uploads. No Xbox, no pbGL needed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v clang >/dev/null; then CC=clang
elif command -v gcc >/dev/null; then CC=gcc
else echo 'ERROR: host C compiler missing' >&2; exit 1
fi
output="$(mktemp)"
trap 'rm -f "$output"' EXIT
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -I"$ROOT/tests/gpu_host/include" -I"$ROOT/include" \
  "$ROOT/src/xssh_gpu.c" "$ROOT/tests/gpu_host/main.c" -o "$output"
"$output"

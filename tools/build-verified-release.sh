#!/usr/bin/env bash
# Build everything from THIS source tree, refusing to distribute a stale XBE.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
NXDK_DIR="${NXDK_DIR:-$HOME/nxdk}"
if [[ ! -f "$NXDK_DIR/Makefile" ]]; then
  echo "ERROR: nxdk not found at $NXDK_DIR; set NXDK_DIR=/path/to/nxdk" >&2
  exit 1
fi
# nxdk-cc lives in the SDK bin directory; do not require the caller to
# manually activate the toolchain or adjust PATH.
export NXDK_DIR
export PATH="$NXDK_DIR/bin:$PATH"
if ! command -v nxdk-cc >/dev/null 2>&1; then
  echo "ERROR: nxdk-cc not found in $NXDK_DIR/bin (check the nxdk installation)" >&2
  exit 1
fi
# Safe to run repeatedly, even with the bundled patched libssh2 tree.
"$ROOT/tools/fetch-deps.sh"
if [[ "${GPU_ACCELERATION:-y}" == y ]]; then
  "$ROOT/tools/fetch-pbgl.sh"
fi
if [[ ! -f third_party/libssh2/src/mbedtls.c || ! -f third_party/mbedtls/include/mbedtls/sha256.h || ! -s third_party/dr_mp3/dr_mp3.h ]]; then
  echo 'ERROR: required dependency source/header missing after fetch-deps.sh' >&2
  exit 1
fi
# Reject blank or corrupted bitmap fonts before cross-compiling.
python3 tools/check-font-atlas.py
python3 tools/test-antialiasing.py
# Leave the existing seed alone, but never leave an old XBE in release/
# if the new compilation or verification fails.
rm -f release/default.xbe
jobs="${JOBS:-4}"
make -B -j"$jobs" NXDK_DIR="$NXDK_DIR" GPU_ACCELERATION="${GPU_ACCELERATION:-y}" release
python3 tools/verify-built-xbe.py release/default.xbe
if ! cmp -s bin/default.xbe release/default.xbe; then
  echo 'ERROR: packaged XBE differs from compiled XBE' >&2
  exit 1
fi
echo 'PASS: release/default.xbe is verified and byte-identical to bin/default.xbe'
sha256sum bin/default.xbe release/default.xbe
printf '\nCopy release/default.xbe and release/xssh.cfg to the Xbox.\n'
printf 'For a NEW install copy release/xssh-seed.bin too; preserve the existing Xbox seed on upgrades.\n'

#!/usr/bin/env bash
# pbGL is an MIT-licensed external GPU renderer for nxdk. Keep it separate
# from the bundled XSSH code: the user can update or pin it independently.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PBGL_DIR="${PBGL_DIR:-$ROOT/third_party/pbgl}"
if [[ -f "$PBGL_DIR/config_pbgl.make" && -f "$PBGL_DIR/include/pbgl.h" ]]; then
  echo "pbGL ready at $PBGL_DIR"
  exit 0
fi
if [[ -e "$PBGL_DIR" ]]; then
  echo "ERROR: $PBGL_DIR exists but is not a complete pbGL checkout" >&2
  exit 1
fi
command -v git >/dev/null || { echo 'ERROR: git required to fetch pbGL' >&2; exit 1; }
git clone --depth 1 https://github.com/fgsfdsfgs/pbgl.git "$PBGL_DIR"
[[ -f "$PBGL_DIR/config_pbgl.make" && -f "$PBGL_DIR/include/pbgl.h" ]] || {
  echo 'ERROR: pbGL download incomplete' >&2; exit 1;
}
echo "pbGL source: $(git -C "$PBGL_DIR" rev-parse HEAD)"

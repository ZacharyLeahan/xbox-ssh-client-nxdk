#!/usr/bin/env bash
# Idempotent dependency setup. Preserve bundled/patched libssh2 sources.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p third_party

ensure_git_source() {
  local name="$1" tag="$2" url="$3" marker="$4" dest="third_party/$1"
  if [[ -f "$dest/$marker" ]]; then
    echo "[ deps ] Using existing $name source ($dest)"
    return
  fi
  if [[ -e "$dest" ]]; then
    echo "ERROR: $dest exists but lacks $marker; move it aside or restore a complete source tree." >&2
    exit 1
  fi
  command -v git >/dev/null || { echo 'ERROR: git is required to fetch missing dependencies' >&2; exit 1; }
  echo "[ deps ] Fetching $name ($tag)"
  git clone --depth 1 --branch "$tag" "$url" "$dest"
  [[ -f "$dest/$marker" ]] || { echo "ERROR: missing $dest/$marker after clone" >&2; exit 1; }
}

ensure_git_source libssh2 libssh2-1.11.1 https://github.com/libssh2/libssh2.git src/mbedtls.c
ensure_git_source mbedtls v3.6.5 https://github.com/Mbed-TLS/mbedtls.git include/mbedtls/sha256.h

# Small standalone MP3 decoder, pinned to the project's original revision.
decoder_hash=7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356
mkdir -p third_party/dr_mp3
if ! printf '%s  %s\n' "$decoder_hash" third_party/dr_mp3/dr_mp3.h | sha256sum -c --status 2>/dev/null; then
  command -v curl >/dev/null || { echo 'ERROR: curl is required for missing dr_mp3.h' >&2; exit 1; }
  tmp=third_party/dr_mp3/dr_mp3.h.tmp
  trap 'rm -f "$tmp"' EXIT
  curl -fL --retry 3 --connect-timeout 15 \
    https://raw.githubusercontent.com/mackron/dr_libs/f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c/dr_mp3.h -o "$tmp"
  printf '%s  %s\n' "$decoder_hash" "$tmp" | sha256sum -c
  mv "$tmp" third_party/dr_mp3/dr_mp3.h
  trap - EXIT
fi
python3 tools/patch-libssh2-nxdk.py
printf '[ deps ] Ready: libssh2, Mbed TLS, dr_mp3; Xbox compatibility patches applied.\n'

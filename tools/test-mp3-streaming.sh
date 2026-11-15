#!/usr/bin/env bash
# Native integration test. Requires ffmpeg, SDL2 development files, and fetched dr_mp3.
set -euo pipefail
cd "$(dirname "$0")/.."
test_directory=$(mktemp -d)
trap 'rm -rf "$test_directory"' EXIT
sdl_flags=${SDL_CFLAGS:-$(pkg-config --cflags sdl2)}
sdl_libraries=${SDL_LIBS:-$(pkg-config --libs sdl2)}
cc -std=gnu11 -O1 $sdl_flags -Itools/stream-test-stubs -Iinclude   -Ithird_party/nxdk-audio/include -Itools/audio-test-stubs -Ithird_party/dr_mp3   tools/test-mp3-streaming.c $sdl_libraries -lm -o "$test_directory/test-stream"
ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=330'   -ar 44100 -ac 2 -codec:a libmp3lame -b:a 128k "$test_directory/long.mp3"
ffmpeg -v error -f lavfi -i 'sine=frequency=660:duration=2'   -ar 22050 -ac 1 -codec:a libmp3lame -b:a 64k "$test_directory/short.mp3"
ffmpeg -v error -f lavfi -i 'sine=frequency=777:duration=150' -ar 44100 -ac 2 -codec:a libmp3lame -q:a 3 "$test_directory/vbr.mp3"
python3 - "$test_directory" <<'PY'
import sys
from pathlib import Path
directory=Path(sys.argv[1])
for name in ("long.mp3","short.mp3","vbr.mp3"):
    (directory/("D:"+chr(92)+"mp3s"+chr(92)+name)).symlink_to(directory/name)
PY
"$test_directory/test-stream" "$test_directory"
"$test_directory/test-stream" "$test_directory" seek

"$test_directory/test-stream" "$test_directory" checkpoints

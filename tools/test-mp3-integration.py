#!/usr/bin/env python3
"""Static integration checks; native USB key behavior covered by test-hid-keys.c."""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
main=(root/'src/main.c').read_text()
mp3=(root/'src/xssh_mp3.c').read_text()
hid=(root/'src/hid_keys.c').read_text()
make=(root/'Makefile').read_text()
assert 'xssh_mp3_pump(&a->music)' in main
assert 'xssh_mp3_stop(&a->music)' in main
assert 'XSSH_KEY_MUSIC_TOGGLE' in main and 'SDLK_p' in main
assert 'XSSH_KEY_MUSIC_VOLUME_UP' in hid and 'XSSH_KEY_MUSIC_VOLUME_DOWN' in hid
assert '#define MUSIC_DIR "D:\\\\mp3s"' in mp3
assert 'xssh_audio_poll' in mp3 and 'xssh_audio_begin' in mp3
assert 'drmp3_read_pcm_frames_s16' in mp3
assert 'xssh_audio_stop' in mp3
assert 'src/xssh_mp3.c' in make
assert 'third_party/dr_mp3' in make
print('PASS: MP3 source wiring, queued PCM, folder, device lifecycle and local shortcuts')

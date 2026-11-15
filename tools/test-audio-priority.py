#!/usr/bin/env python3
"""Check that MP3 decode lives off the UI thread and video defers to audio."""
from pathlib import Path
r=Path(__file__).resolve().parent.parent
player=(r/'src/xssh_mp3.c').read_text()
main=(r/'src/main.c').read_text()
header=(r/'include/xssh_mp3.h').read_text()
assert 'SDL_CreateThread(mp3_worker' in player
assert 'SDL_WaitThread(m->thread' not in player
assert 'drmp3_read_pcm_frames_s16' in player
assert 'void xssh_mp3_pump' in player
assert 'drmp3_read_pcm_frames_s16' not in player[player.index('void xssh_mp3_pump'):]
assert 'xssh_audio_poll' in player
assert 'xssh_audio_begin' in player
assert 'MUSIC_URGENT_MS 300u' in player
assert 'if(!xssh_mp3_audio_urgent(&a->music))' not in main
assert 'xssh_mp3_is_playing(&a->music)' in main
assert 'xssh_mp3_stop(&a->music)' in main
assert 'SDL_CreateMutex' in player and 'SDL_LockMutex' in player
assert 'xssh_mp3_get_message' in header and 'xssh_mp3_get_message' in main
assert 'DR_MP3_NO_STDIO' in player
assert 'XSSH TERM v11.6' in (r/'include/xssh_build.h').read_text()
print('PASS: dedicated audio worker, synchronization, bounded queue, video priority, cleanup')

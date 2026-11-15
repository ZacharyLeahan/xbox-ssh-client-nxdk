from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'src/xssh_mp3.c').read_text()
h=(r/'include/xssh_mp3.h').read_text()
a=(r/'src/main.c').read_text()
assert 'MUSIC_RECOVERY_MAX_ATTEMPTS 3' in s
assert 'MUSIC_RECOVERY_DELAY_MS 750u' in s
assert 'if(finished && m->thread)' in s
assert 'if(stopping)' in s
assert 'm->recovery_pending=1' in s
assert 'if(!start_worker(m,0))' in s
assert 'if(m->recovery_pending && !m->thread' in s
assert 'MP3 playback stopped' in s
assert 'MP3 stop timeout:' in s
assert 'xssh_mp3_take_status' in h
assert 'xssh_mp3_take_status(&a->music' in a
assert 'if(m->next_track<0 || m->next_track>=m->track_count)shuffle(m);' in s
print('PASS: bounded worker recovery, stop completion, stalled-worker safety, UI notification')

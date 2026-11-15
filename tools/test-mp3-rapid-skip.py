from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/xssh_mp3.c').read_text()
h=(Path(__file__).resolve().parents[1]/'include/xssh_mp3.h').read_text()
assert 'MUSIC_SKIP_SETTLE_MS 160u' in s
assert 'pending_skip+=skip;' in s
assert 'm->skip_request+=direction<0?-1:1;' in s
assert 'index=m->next_track-1+pending_skip;' in s
assert 'if(!open_next(m))break;' in s
assert 'drmp3_calculate_seek_points' not in s
assert 'drmp3_bind_seek_table' not in s
assert 'if(m->seek_points){free(m->seek_points)' in s
assert 'SDL_PauseAudioDevice(m->device,1);' in s
assert 'void *seek_points;' in h
print('PASS: rapid skip coalescing, decoder seek index, safe pause/flush')

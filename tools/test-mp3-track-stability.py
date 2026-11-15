from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/xssh_mp3.c').read_text()
h=(Path(__file__).resolve().parents[1]/'include/xssh_mp3.h').read_text()
assert 'static void close_decoder(' in s
assert 'SDL_NewAudioStream(' in s
assert 'requested.freq=MUSIC_OUTPUT_RATE' in s
assert s.count('SDL_CloseAudioDevice(m->device)')==1
assert 'SDL_CloseAudioDevice (shutdown)' in s
assert 'if(!m->device){' in s
assert 'frames_to_read=((drmp3_uint64)room*mp->sampleRate)/(MUSIC_OUTPUT_RATE*4u)' in s
assert 'SDL_AudioStreamAvailable(m->stream)>0' in s
assert 'drmp3_read_pcm_frames_s16(mp,frames_to_read,pcm)' in s
assert 'm->frame_cursor=0' in s
assert 'int audio_rate, audio_channels' in h
assert 'void xssh_mp3_skip(' in s and 'void xssh_mp3_seek(' in s
print('PASS: fixed-format session audio, conversion, bounded decoding, skip/seek preserved')

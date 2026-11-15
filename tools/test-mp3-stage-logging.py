from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/xssh_mp3.c').read_text()
assert 'drmp3_calculate_seek_points(' not in s
for name in ('SDL_OpenAudioDevice (session)','SDL_CloseAudioDevice (shutdown)','drmp3_init','drmp3_read_pcm_frames_s16','SDL_QueueAudio','fopen MP3'):
    assert f'worker_stage(m,"{name}")' in s,name
assert 'D:\\\\xssh-mp3.log' in s
assert 'if(event[0])log_event_main(event,stage,track)' in s
assert 'MP3 stop requested' in s
print('PASS: disabled eager seek scan, stage logging and stop acknowledgement')

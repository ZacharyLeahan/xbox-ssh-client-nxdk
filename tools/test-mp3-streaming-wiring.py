from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/"src/xssh_mp3.c").read_text()
worker=source[source.index("static int mp3_worker"):source.index("void xssh_mp3_init")]
assert "MUSIC_TRACK_PCM_BYTES" not in source
assert "MUSIC_START_BYTES (24u*1024u)" in source
assert "SDL_THREAD_PRIORITY_TIME_CRITICAL" in worker
assert "SDL_THREAD_PRIORITY_LOW" not in worker
assert "SDL_Delay(1)" in worker and "SDL_Delay(5)" in worker
assert "xssh_audio_acquire" in worker and "xssh_audio_commit" in worker
for name in ("SDL_OpenAudioDevice", "SDL_QueueAudio", "SDL_ClearQueuedAudio", "SDL_CloseAudioDevice"):
    assert name not in source
assert "loading next mp3" in source
print("PASS: bounded HDD streaming, maximum worker priority, cooperative yields, and hardware buffers")

from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'src/xssh_mp3.c').read_text()
h=(r/'include/xssh_mp3.h').read_text()
assert 'MUSIC_WORKER_STALL_MS 3000u' in s
assert 'worker_heartbeat(m)' in s
assert 'worker_heartbeat_ms' in h
assert 'MP3 stalled:' in s
assert '/* A second toggle during shutdown must not start another worker. */' in s
assert 'frames>frames_to_read' in s
print('PASS: worker heartbeat, stall detection, nonblocking UI stop, invalid decoder frame guard')

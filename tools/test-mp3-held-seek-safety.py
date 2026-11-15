from pathlib import Path
src=Path('src/xssh_mp3.c').read_text()
hid=Path('src/hid_keys.c').read_text()
assert 'seek_request' in src
assert 'seek_direction' not in src
assert 'seek_lease_ms' not in src
assert 'Refresh the seek lease' not in hid
assert 'drmp3_seek_to_pcm_frame' in src
assert 'track_total_frames' in src
print('One-shot MP3 seek safety passed')

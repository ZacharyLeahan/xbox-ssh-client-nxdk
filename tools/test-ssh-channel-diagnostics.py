#!/usr/bin/env python3
from pathlib import Path
s=(Path(__file__).parent.parent/'src/main.c').read_text()
assert 'libssh2_channel_open_ex(a->session,"session",7,' in s
assert '256u*1024u,32u*1024u,NULL,0' in s
assert 'libssh2_session_last_error(a->session,&detail,&len,0)' in s
assert 'SSH channel failed (%d): %.*s' in s
assert 'libssh2_userauth_password(a->session' in s
m=(Path(__file__).parent.parent/'src/xssh_mp3.c').read_text()
assert '#define DR_MP3_NO_STDIO' in m
assert 'drmp3_init(mp,mp3_read,mp3_seek,mp3_tell,NULL,f,NULL)' in m
assert 'drmp3_init_file' not in m
assert 'fclose(m->track_file)' in m
print('PASS: SSH channel diagnostics, bounded window, MP3 nxdk callback stream')

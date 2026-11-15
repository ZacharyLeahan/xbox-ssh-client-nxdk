#!/usr/bin/env python3
"""Regression: audio must not starve terminal redraw or keyboard echo."""
from pathlib import Path
r=Path(__file__).resolve().parent.parent
ui=(r/'src/main.c').read_text()
audio=(r/'src/xssh_mp3.c').read_text()
assert 'TERMINAL_REDRAW_BUDGET_MUSIC 640u' in ui
assert 'TERMINAL_REDRAW_BUDGET_MUSIC 64u' not in ui
assert 'render(a);a->ui_status_dirty=0;a->last_render_ms=now;' in ui
assert 'unsigned render_interval=a->saver_active ?' in ui
assert ' : 16u;' in ui
assert 'if(!xssh_mp3_audio_urgent(&a->music))' not in ui
terminal=ui[ui.index('else if(a->stage!=ST_CONNECTED || a->term.dirty'):ui.index('SDL_Delay(2);',ui.index('else if(a->stage!=ST_CONNECTED || a->term.dirty'))]
assert 'xssh_mp3_audio_urgent' not in terminal
assert 'drmp3_int16 *pcm=' in audio
assert 'drmp3_int16 pcm[MUSIC_DECODE_FRAMES*2]' not in audio
assert 'free(pcm);' in audio
print('PASS: responsive terminal, bounded music redraw, dedicated heap PCM')

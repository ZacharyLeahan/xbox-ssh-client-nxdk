#!/usr/bin/env python3
"""Checks the screensaver -> busy terminal redraw regression in the real UI code."""
from pathlib import Path
s=(Path(__file__).resolve().parent.parent/'src/main.c').read_text()
start=s[s.index('static void saver_stop(App *a){'):s.index('static void saver_start(App *a,')]
assert 'a->term.dirty=1;' in start
assert 'a->cache_valid=0' not in start, 'Waking must not discard the entire terminal cache'
assert 'SDL_DestroyTexture(a->saver_texture)' in start
assert 'TERMINAL_REDRAW_BUDGET 320u' in s
redraw=s[s.index('static void draw_terminal(App *a,'):s.index('static void resize_for_keyboard(App *a,')]
assert 'a->cache_scan_cursor+budget' in redraw
assert 'TERMINAL_REDRAW_BUDGET_MUSIC' in redraw
assert 'a->cache_scan_again=1' in redraw, 'Concurrent SSH output must trigger another pass'
assert 'a->term.dirty=0;' in redraw
assert 'a->cache_scan_active=0' in redraw
assert 'a->cache_scan_active=0;a->cache_scan_again=0;' in redraw, 'Fallback must not keep requesting scans'
assert 'a->cache_scan_active' in s[s.index('unsigned render_interval='):s.index('SDL_Delay(2);')]
print('PASS: screensaver wake retains terminal cache, redraw budget, repeated busy-terminal pass, safe fallback')

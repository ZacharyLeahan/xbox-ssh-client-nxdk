from pathlib import Path
s=Path('src/main.c').read_text()
assert 'e->keysym.sym==SDLK_t){toggle_theme(a);return;}' in s
assert 'a->cache_valid=0;' in s[s.index('static void toggle_theme'):s.index('static void keyboard_event')]
assert 'a->volume_status : a->ram_status' in s
assert 'a->ui_status_dirty' in s
assert 'status_bar_only(a,msg);' in s[s.index('static void music_toggle'):s.index('static void music_volume')]
assert 'snprintf(a->volume_status' in s[s.index('static void music_volume'):s.index('static void toggle_theme')]
assert 'overclock' not in s.lower()
print('PASS: MP3 bottom-bar status and Ctrl+T theme shortcuts')

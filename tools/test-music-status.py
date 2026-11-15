#!/usr/bin/env python3
"""Verify local music shortcuts cannot print into the remote terminal grid."""
from pathlib import Path
import re
main=(Path(__file__).resolve().parent.parent/'src/main.c').read_text()
helper=re.search(r'static void status_bar_only\(App \*a,const char \*s\) \{(.*?)\n\}',main,re.S)
assert helper and 'snprintf(a->status' in helper.group(1)
assert 'logline' not in helper.group(1) and 'term_feed' not in helper.group(1)
for name in ['music_toggle']:
    function=re.search(r'static void '+name+r'\([^)]*\) \{(.*?)\n\}',main,re.S)
    assert function, name
    assert 'status_bar_only(a,msg);' in function.group(1)
    assert 'status(a,msg)' not in function.group(1)
assert 'snprintf(a->volume_status' in main[main.index('static void music_volume('):main.index('static void music_skip(')]
assert 'logline(a,s);' in main[main.index('static void status('):main.index('static void status_bar_only(')]
print('PASS: MP3 toggle and volume changes update status bar without altering terminal content')

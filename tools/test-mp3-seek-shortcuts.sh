#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -Werror -Iinclude src/hid_keys.c tools/test-mp3-seek-shortcuts.c -o /tmp/xssh-mp3-seek-test
/tmp/xssh-mp3-seek-test

#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# The product code carries no publisher or game branding, no developer mode
# and no environment switch (spec 3, anti-goals).
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/src"
fail=0
deny() {  # deny <what> <grep -E pattern> [flags]
    local hits
    hits=$(grep -rnE ${3:-} "$2" "$src" || true)
    if [ -n "$hits" ]; then echo "  FAIL $1:"; echo "$hits" | sed 's/^/    /'; fail=1; fi
}
# 1.1: one exception, the registry path the game reads (jk_prefix.c GAME_KEY, never shown to the player)
hits=$(grep -rnEi 'Need for Speed|Electronic Arts|EA Games|Black Box|Rockport' "$src" \
       | grep -v '^[^:]*/jk_prefix\.c:[0-9]*:static const char GAME_KEY\[\] = .*/\* registry-path \*/$' || true)
if [ -n "$hits" ]; then echo "  FAIL publisher or series name:"; echo "$hits" | sed 's/^/    /'; fail=1; fi
deny "environment switch" 'getenv|secure_getenv|environ\b'
deny "developer mode" 'dev[ _-]?mode|maintenance|debug menu|cheat|hidden menu' -i
deny "marker or switch file" '\.marker|dev\.txt|test[-_]mode' -i
deny "build host path" '/home/|srutom|orbis-win-host-bootstrap'
# the only test hook is the fault-injection counter, set by code only
n=$(grep -rn "jk_fs_fail_after" "$src" | grep -vc "jk_fs\.[ch]" || true)
if [ "$n" != 0 ]; then echo "  FAIL jk_fs_fail_after used outside jk_fs"; fail=1; fi
if [ $fail = 0 ]; then echo "test_no_branding: ok"; else echo "test_no_branding: FAILED"; fi
exit $fail

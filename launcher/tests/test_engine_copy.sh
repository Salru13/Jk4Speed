#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# The mods engine's player-facing text (JK_S_E_* in jk_strings.h) never asks
# the player to copy game files (constraints: the one explanation lives in
# Help > Troubleshooting) and uses the r4 words: "Patch" and "Jk4Speed", not
# "PATCH" / "JK4SPEED", and no engine jargon.
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
f="$root/src/jk_strings.h"
fail=0
lines=$(grep -E '^#define JK_S_E_' "$f" | sed -E 's/^#define [A-Z0-9_]+ //')   # the values only
n=$(printf '%s\n' "$lines" | grep -c . || true)
check() {  # check <what> <grep -E pattern> [flags]
    local hits
    hits=$(printf '%s\n' "$lines" | grep -E ${3:-} "$2" || true)
    if [ -n "$hits" ]; then echo "  FAIL $1:"; echo "$hits" | sed 's/^/    /'; fail=1; fi
}
check "asks to copy game files" 'cop(y|ies).*game|game.*cop(y|ies)' -i
check "PATCH or JK4SPEED in capitals" 'PATCH|JK4SPEED'
check "engine jargon" 'pristine|journal|delta|inconsistent|untouched|disk full' -i
[ "$n" -ge 30 ] || { echo "  FAIL only $n JK_S_E_ strings found"; fail=1; }
if [ $fail = 0 ]; then echo "test_engine_copy: ok ($n strings)"; else echo "test_engine_copy: FAILED"; fi
exit $fail

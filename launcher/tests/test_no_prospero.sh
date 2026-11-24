#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# No player-facing text names the bench runtime ("prospero" in any letter
# case): the launcher's strings and the player docs (constraints, r4). The
# title's README is for builders and is not checked.
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$root/.." && pwd)"
fail=0
for f in "$root/src/jk_strings.h" "$repo/docs/launcher/README-player.md" "$repo/docs/launcher/FAQ.md"; do
    [ -f "$f" ] || { echo "  (not there yet, skipped: ${f#$repo/})"; continue; }
    hits=$(grep -niE 'p.?r.?o.?s.?p.?e.?r.?o' "$f" || true)
    if [ -n "$hits" ]; then echo "  FAIL $f:"; echo "$hits" | sed 's/^/    /'; fail=1; fi
done
if [ $fail = 0 ]; then echo "test_no_prospero: ok"; else echo "test_no_prospero: FAILED"; fi
exit $fail

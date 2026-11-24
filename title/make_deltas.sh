#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Build the 7 JKD1 deltas that let the launcher make our derived files from the
# player's own files: the game, the X360 Stuff Pack, and the mod authors' own
# releases (N4SRazer's HD Road Textures ZIP, LunicAura106's Upscaled HUD 7z,
# extracted as released). launcher/title/author_deltas.py does the work; every
# input is a local, git-ignored copy checked by hash, and every delta is
# refused if its literal bytes hold a 64-byte block of any of its sources
# (author files included). Output: $JK_TITLE_OUT/deltas/*.jkd, DELTAS.json, DELTAS.txt
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
B="${JK_BOOTSTRAP:-$HOME/orbis-win-host-bootstrap}"
OUT="${JK_TITLE_OUT:-$B/jk4speed-title-r1}"
D="$OUT/deltas"
rm -rf "$D"
mkdir -p "$D"
python3 -I "$here/author_deltas.py" "$D"
(cd "$D" && for f in *.jkd; do
    python3 -I "$launcher/tools/make_delta.py" info "$f" | sed "s#^#$f: #"
 done; sha256sum *.jkd) | tee "$D/DELTAS.txt"

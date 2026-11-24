#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Generate jk_font.h / jk_font_data.c (Ubuntu Sans glyph atlases) into OUTDIR.
# Host tool; needs a C compiler and FreeType (headers via FREETYPE_INCLUDE,
# library via FREETYPE_LIB). Outputs are rebuilt only when an input changed.
set -euo pipefail
out="${1:?usage: bake_assets.sh OUTDIR}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mkdir -p "$out"

: "${FREETYPE_INCLUDE:=$HOME/orbis-win-host-bootstrap/native248-build-wine/freetype/src/include}"
: "${FREETYPE_LIB:=/usr/lib/x86_64-linux-gnu/libfreetype.so.6}"

fonts="$root/assets/font"
up="$fonts/UbuntuSans-VF.ttf"
it="$fonts/UbuntuSans-Italic-VF.ttf"
if [ ! -f "$out/jk_font_data.c" ] || [ "$here/bake_font.c" -nt "$out/jk_font_data.c" ] \
   || [ "$up" -nt "$out/jk_font_data.c" ] || [ "$it" -nt "$out/jk_font_data.c" ]; then
    cc -std=c11 -O2 -Wall -Wextra -I"$FREETYPE_INCLUDE" -o "$out/bake_font" "$here/bake_font.c" "$FREETYPE_LIB" -lm
    "$out/bake_font" "$up" "$it" "$out/jk_font.h" "$out/jk_font_data.c"
fi

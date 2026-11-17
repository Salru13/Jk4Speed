#!/bin/bash
# Build mwprobe.exe (PE32 i386, MinGW) into build/probe/ of this repository.
# Linux or WSL: bash probe/build.sh
set -eu
SRC_DIR=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$SRC_DIR/../build/probe}
CC=${CC:-i686-w64-mingw32-gcc}
mkdir -p "$OUT"
cp "$SRC_DIR/mwprobe.c" "$OUT/mwprobe.c"
# speed.exe flags: base 0x400000, relocs stripped, no NX_COMPAT, no LAA.
"$CC" -O1 -std=gnu99 -Wall -Wextra -mconsole -static-libgcc \
  -fno-stack-protector \
  -o "$OUT/mwprobe.exe" "$OUT/mwprobe.c" \
  -Wl,--image-base,0x400000 \
  -Wl,--disable-nxcompat \
  -Wl,--disable-dynamicbase \
  -Wl,--disable-large-address-aware \
  -Wl,--disable-reloc-section \
  -Wl,--no-insert-timestamp \
  -s
sha256sum "$OUT/mwprobe.exe"
ls -l "$OUT/mwprobe.exe"

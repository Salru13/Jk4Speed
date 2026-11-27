# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Refuse a title PRX that the console's loader cannot relocate (1.10, 2026-11-27).

create-fself writes the last RW segment 0x4d0 bytes shorter than the linker
made it (the .got.plt is taken out). A relocation or a symbol in that cut tail
makes rtld refuse the module (reloc_non_plt, load_prx 0x80020008) and the title
stays on its start picture. build_title.sh pads .bss so the tail is empty;
this check proves it from the two files.

usage: check_prx_tail.py <app.shared.elf> <relocated_wine_app.prx>
"""
import struct
import subprocess
import sys


def loads(data, base):
    phoff, = struct.unpack_from('<Q', data, base + 0x20)
    phn, = struct.unpack_from('<H', data, base + 0x38)
    out = []
    for i in range(phn):
        t, _f, _off, va, _pa, _fs, ms, _al = struct.unpack_from('<IIQQQQQQ', data, base + phoff + i * 56)
        if t == 1:
            out.append((va, va + ms))
    return out


elf_path, prx_path = sys.argv[1], sys.argv[2]
elf = open(elf_path, 'rb').read()
prx = open(prx_path, 'rb').read()
elf_end = max(e for _, e in loads(elf, 0))
prx_end = max(e for _, e in loads(prx, prx.find(b'\x7fELF')))
bad = []
# rtld maps whole 16 KB pages: what lies below the next page boundary is
# still mapped (1.00/1.01 and r3 had their tail objects there, by luck).
mapped_end = (prx_end + 0x3fff) & ~0x3fff
if mapped_end < elf_end:
    lo, hi = mapped_end, elf_end
    rel = subprocess.run(['readelf', '-rW', elf_path], capture_output=True, text=True, check=True).stdout
    for line in rel.splitlines():
        f = line.split()
        if len(f) < 3 or not f[2].startswith('R_X86_64'):
            continue
        targets = [int(f[0], 16)]
        if f[2] == 'R_X86_64_RELATIVE':
            targets.append(int(f[-1], 16))
        if any(lo <= t < hi for t in targets):
            bad.append(line.strip())
    syms = subprocess.run(['nm', '-n', elf_path], capture_output=True, text=True, check=True).stdout
    for line in syms.splitlines():
        f = line.split()
        # jk_bss_tail: build_title.sh's unused padding, meant to sit there
        if len(f) == 3 and lo <= int(f[0], 16) < hi and f[2] != 'jk_bss_tail':
            bad.append('symbol ' + line.strip())
print(f'prx tail: linker end {elf_end:#x}, loaded end {prx_end:#x} (mapped to {mapped_end:#x}), '
      f'beyond the mapping: {len(bad)}')
for b in bad[:10]:
    print('   ', b)
sys.exit(1 if bad else 0)

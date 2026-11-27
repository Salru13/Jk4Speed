#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Writes <app_root>/jk4speed-files/release.txt (src/jk_release.h format) over a
staged package tree: eboot.bin, sce_sys/param.sfo and every file under
jk4speed-files/ and jk4speed-files-nfs/ except release.txt/release.sig.
  python3 make_release.py APP_ROOT BUILD_ID [--eboot PATH]
--eboot: hash eboot.bin from PATH (make_pkg.sh keeps it outside the stage)."""
import hashlib, os, sys

def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()

def main():
    root, build = sys.argv[1], sys.argv[2]
    eboot = sys.argv[sys.argv.index('--eboot') + 1] if '--eboot' in sys.argv else None
    # r5 console 2026-11-24: the PS4 rewrites eboot.bin at install and keeps
    # param.sfo outside /app0, so only our own files are listed
    rels = []
    base = os.path.join(root, 'jk4speed-files')
    # 1.1: the original game's package folder is ours too (signed like the first)
    walk = [w for b in (base, os.path.join(root, 'jk4speed-files-nfs')) for w in os.walk(b)]
    for d, _, files in walk:
        for n in files:
            rel = os.path.relpath(os.path.join(d, n), root).replace(os.sep, '/')
            if rel in ('jk4speed-files/release.txt', 'jk4speed-files/release.sig'):
                continue
            if os.path.islink(os.path.join(d, n)):
                continue
            rels.append(rel)
    lines = ['JK4SPEED-RELEASE 1', 'build ' + build]
    for rel in sorted(set(rels)):
        if '..' in rel or rel.startswith('/'):
            sys.exit('make_release: bad path ' + rel)
        src = eboot if rel == 'eboot.bin' and eboot else os.path.join(root, rel)
        lines.append('%s  %s' % (sha(src), rel))
    with open(os.path.join(base, 'release.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('release.txt: %d files' % (len(lines) - 2))

if __name__ == '__main__':
    main()

# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Every golden in golden/goldens.txt has a committed PNG that decodes to
exactly that pixel hash, and no PNG is left without an entry."""
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import pack_goldens as pg  # noqa: E402


def main():
    entries = pg.goldens()
    names = {n for n, _ in entries}
    bad = 0
    for name, sha in entries:
        path = os.path.join(pg.ROOT, 'golden', name + '.png')
        if not os.path.exists(path):
            print('  FAIL %s: no golden PNG' % name)
            bad += 1
            continue
        w, h, rgb = pg.read_png(path)
        if (w, h) != (1920, 1080) or hashlib.sha256(pg.bgrx(rgb)).hexdigest() != sha:
            print('  FAIL %s: PNG does not match goldens.txt' % name)
            bad += 1
    for f in os.listdir(os.path.join(pg.ROOT, 'golden')):
        if f.endswith('.png') and f[:-4] not in names:
            print('  FAIL %s: stale golden PNG' % f)
            bad += 1
    print('test_goldens_png: %d goldens, %d failed' % (len(entries), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

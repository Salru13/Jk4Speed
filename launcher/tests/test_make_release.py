# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""tools/make_release.py: the signed file list behind the Official Build badge.

Like JKPro's (on the console 2026-11-26: Official Build), the list also covers the launcher itself
(sce_module/relocated_wine_app.prx) and the native DXVK server (sce_module/dxvknative.prx), given with
--add REL=SRC at their installed paths. eboot.bin stays out: the PS4 rewrites it at install (r5 test)."""
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), 'tools', 'make_release.py')
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('  FAIL', what)


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)


with tempfile.TemporaryDirectory() as d:
    app = os.path.join(d, 'app')
    put(os.path.join(app, 'jk4speed-files', 'd3d9.dll'), b'dll')
    put(os.path.join(app, 'jk4speed-files-nfs', 'scripts', 'w.asi'), b'asi')
    put(os.path.join(app, 'sce_module', 'dxvknative.prx'), b'server')
    prx = os.path.join(d, 'build', 'relocated_wine_app.prx')          # built outside the stage
    put(prx, b'launcher')
    r = subprocess.run([sys.executable, '-I', TOOL, app, 'JK4SPEED 1.1',
                        '--add', 'sce_module/relocated_wine_app.prx=' + prx,
                        '--add', 'sce_module/dxvknative.prx=' + os.path.join(app, 'sce_module', 'dxvknative.prx')],
                       capture_output=True, text=True)
    check(r.returncode == 0, 'runs: ' + r.stderr)
    text = open(os.path.join(app, 'jk4speed-files', 'release.txt')).read()
    lines = text.splitlines()
    check(lines[:2] == ['JK4SPEED-RELEASE 1', 'build JK4SPEED 1.1'], 'header')
    want = {
        'jk4speed-files/d3d9.dll': b'dll',
        'jk4speed-files-nfs/scripts/w.asi': b'asi',
        'sce_module/relocated_wine_app.prx': b'launcher',
        'sce_module/dxvknative.prx': b'server',
    }
    got = {l[66:]: l[:64] for l in lines[2:]}
    check(set(got) == set(want), 'listed exactly: %s' % sorted(got))
    for rel, data in want.items():
        check(got.get(rel) == hashlib.sha256(data).hexdigest(), 'sha of ' + rel)
    check('eboot.bin' not in got, 'eboot.bin stays out')
    check(lines[2:] == sorted(lines[2:], key=lambda l: l[66:]), 'sorted by path')

    bad = subprocess.run([sys.executable, '-I', TOOL, app, 'x', '--add', '../evil=' + prx], capture_output=True, text=True)
    check(bad.returncode != 0, 'a path outside the app is refused')
    miss = subprocess.run([sys.executable, '-I', TOOL, app, 'x', '--add', 'sce_module/x.prx=' + os.path.join(d, 'nope')],
                          capture_output=True, text=True)
    check(miss.returncode != 0, 'a missing source is refused')

print('test_make_release: %s' % ('ok' if not fails else 'FAILED (%d)' % fails))
sys.exit(1 if fails else 0)

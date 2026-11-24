# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""tools/make_manifest.py: builds jk4speed-files/manifest.txt for the patcher."""
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), 'tools', 'make_manifest.py')
V13 = '80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c'
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('  FAIL', what)


def put(root, rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)


def run(*args):
    return subprocess.run([sys.executable, '-I', TOOL] + list(args), capture_output=True, text=True)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def builds_manifest():
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'jk4speed-files')
        put(files, 'd3d9.dll', b'client')
        put(files, 'dinput8.dll', b'loader')
        put(files, 'scripts/Fix.asi', b'asi')
        put(files, 'scripts/Fix.cfg', b'cfg')
        put(files, 'nfsmw.reg', b'REGEDIT4\n')
        put(d, 'require.txt', b'# game paths\nGLOBAL\nCARS\n')
        put(d, 'remove.txt', b'dsound.dll\nscripts/Old.asi\n')
        r = run(files, '--require-list', os.path.join(d, 'require.txt'), '--remove-list', os.path.join(d, 'remove.txt'),
                '--config', 'scripts/Fix.cfg', '--seed', 'nfsmw.reg')
        check(r.returncode == 0, 'exit 0 (%s)' % r.stderr.strip())
        text = open(os.path.join(files, 'manifest.txt')).read()
        lines = [l for l in text.splitlines() if l and not l.startswith('#')]
        check(lines[0] == 'exe speed.exe ' + V13, 'exe line first, v1.3 hash')
        check('require CARS' in lines and 'require GLOBAL' in lines, 'require lines')
        check('file d3d9.dll %s 6' % sha(b'client') in lines, 'file line with sha and size')
        check('file scripts/Fix.asi %s 3' % sha(b'asi') in lines, 'scripts file')
        check('config scripts/Fix.cfg %s 3' % sha(b'cfg') in lines, 'config line')
        check('remove dsound.dll' in lines and 'remove scripts/Old.asi' in lines, 'remove lines')
        check('seed nfsmw.reg %s 9' % sha(b'REGEDIT4\n') in lines, 'seed line')
        check(not any(' manifest.txt ' in l for l in lines), 'manifest not listed in itself')
        kinds = [l.split()[0] for l in lines]
        order = ['exe', 'require', 'file', 'config', 'remove', 'seed']
        check(kinds == sorted(kinds, key=order.index), 'grouped by kind')
        # a second run gives the same bytes
        r = run(files, '--require-list', os.path.join(d, 'require.txt'), '--remove-list', os.path.join(d, 'remove.txt'),
                '--config', 'scripts/Fix.cfg', '--seed', 'nfsmw.reg')
        check(open(os.path.join(files, 'manifest.txt')).read() == text, 'deterministic')


def rejects_game_names():
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'f')
        put(files, 'd3d9.dll', b'x')
        put(files, 'GLOBAL/something.bun', b'game data')
        r = run(files)
        check(r.returncode != 0, 'a game path in the package is refused')
        check('GLOBAL/something.bun' in r.stderr, 'names the bad path')
        check(not os.path.exists(os.path.join(files, 'manifest.txt')), 'no manifest written')


def rejects_bad_remove():
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'f')
        put(files, 'd3d9.dll', b'x')
        put(d, 'remove.txt', b'speed.exe\n')
        r = run(files, '--remove-list', os.path.join(d, 'remove.txt'))
        check(r.returncode != 0, 'removing speed.exe is refused')


def other_exe_hash():
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'f')
        put(files, 'd3d9.dll', b'x')
        r = run(files, '--exe', 'speed.exe:' + 'a' * 64)
        check(r.returncode == 0, 'custom exe hash accepted')
        check(('exe speed.exe ' + 'a' * 64) in open(os.path.join(files, 'manifest.txt')).read(), 'custom exe line')
        r = run(files, '--exe', 'speed.exe:xyz')
        check(r.returncode != 0, 'bad exe hash refused')


def r4_no_ini_profile_server():
    """r4: the launcher composes jk4speed-dev.ini and nfsmw.profile, the server ships in sce_module/:
    none of them is a manifest line. The base files and the prefix seed are read, never installed."""
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'f')
        put(files, 'd3d9.dll', b'x')
        put(files, 'jk4speed.ini.base', b'[jk4speed]\n')
        put(files, 'nfsmw.profile.base', b'[application]\n')
        put(files, 'seed/index.txt', b'JKSEED 1\n')
        put(files, 'seed/objects/' + 'a' * 64, b'object')
        put(files, 'deltas/GLOBAL_InGameA.bun.jkd', b'JKD1 delta')
        r = run(files)
        check(r.returncode == 0, 'base files, seed and deltas accepted (%s)' % r.stderr.strip())
        text = open(os.path.join(files, 'manifest.txt')).read()
        for word in ('.base', 'seed/', 'deltas/', 'jk4speed-dev.ini', 'profile', 'server', 'accept'):
            check(word not in text, '%s is not in the manifest' % word)
        put(files, 'jk4speed-dev.ini', b'[jk4speed]\r\nDXVK_PS4_NFS_PACE=30\r\n')
        r = run(files)
        check(r.returncode != 0 and 'jk4speed-dev.ini' in r.stderr, 'a packaged jk4speed-dev.ini is refused (%s)' % r.stderr.strip())
        os.remove(os.path.join(files, 'jk4speed-dev.ini'))
        put(files, 'nfsmw-x360.profile', b'prefix = nfsmw\n')
        r = run(files, '--profile', 'nfsmw-x360.profile')
        check(r.returncode != 0, '--profile is gone')
        os.remove(os.path.join(files, 'nfsmw-x360.profile'))
        put(files, 'dxvknative.prx', b'server')
        r = run(files, '--server', 'dxvknative.prx')
        check(r.returncode != 0, '--server is gone')
        r = run(files)
        check(r.returncode != 0, 'a server file among our files is refused')
        os.remove(os.path.join(files, 'dxvknative.prx'))
        r = run(files, '--accept', 'd3d9.dll:' + 'b' * 64)
        check(r.returncode == 0, 'accept still works for a file line (%s)' % r.stderr.strip())
        r = run(files, '--accept', 'd3d9.dll.missing:' + 'b' * 64)
        check(r.returncode != 0, 'accept without its file refused')
        r = run(files, '--accept', 'd3d9.dll:xyz')
        check(r.returncode != 0, 'bad accept hash refused')


def hd_files_pinned():
    with tempfile.TemporaryDirectory() as d:
        files = os.path.join(d, 'f')
        put(files, 'd3d9.dll', b'x')
        put(files, 'GLOBAL/InGameA.bun', b'not the release build')
        r = run(files)
        check(r.returncode != 0, 'an HD file in the package is refused')
        check('mods folder' in r.stderr, 'says why (%s)' % r.stderr.strip())
        os.remove(os.path.join(files, 'GLOBAL/InGameA.bun'))
        put(files, 'scripts/X360Stuff.asi', b'pack asi')
        r = run(files)
        check(r.returncode != 0, 'an X360 pack file in the package is refused')
        os.remove(os.path.join(files, 'scripts/X360Stuff.asi'))
        put(files, 'FX/COMPILED/car.fx.o', b'pack shader')
        r = run(files)
        check(r.returncode != 0, 'a pack shader in the package is refused')
        os.remove(os.path.join(files, 'FX/COMPILED/car.fx.o'))
        put(files, 'jk4look.ini', b'[look]\n')
        r = run(files)
        check(r.returncode != 0, 'the old look file is no longer one of ours')


for t in (builds_manifest, rejects_game_names, rejects_bad_remove, other_exe_hash, r4_no_ini_profile_server, hd_files_pinned):
    t()
print('test_make_manifest: %d failed' % fails)
sys.exit(1 if fails else 0)

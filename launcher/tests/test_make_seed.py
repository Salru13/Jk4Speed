# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""tools/make_seed.py: the Wine prefix seed (jk4speed-files/seed/) for jk_seed.c."""
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), 'tools', 'make_seed.py')
fails = 0

USER_REG = (b'WINE REGISTRY Version 2\n'
            b';; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n'
            b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders] 1794011076\n'
            b'#time=1dd6e4e5ea96c2c\n'
            b'"Desktop"="C:\\\\users\\\\prospero\\\\Desktop"\n'
            b'"Personal"="C:\\\\users\\\\prospero\\\\Documents"\n\n'
            b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders] 1794011076\n'
            b'#time=1dd6e4e5ea95124\n'
            b'"Desktop"=str(2):"%USERPROFILE%\\\\Desktop"\n'
            b'"Personal"=str(2):"%USERPROFILE%\\\\Documents"\n\n'
            b'[Software\\\\Wine] 1794011076\n'
            b'"Personal"="not a shell folder"\n')


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


def sha(b):
    return hashlib.sha256(b).hexdigest()


def run(*args):
    return subprocess.run([sys.executable, '-I', TOOL] + list(args), capture_output=True, text=True)


def fixture(d):
    """A small prefix (a folder with a space, two links, a runtime twin) and an app root."""
    prefix, app = os.path.join(d, 'prefix'), os.path.join(d, 'uroot')
    put(prefix, 'user.reg', USER_REG)
    put(prefix, 'system.reg', b'WINE REGISTRY Version 2\n')
    put(prefix, 'drive_c/windows/syswow64/ntdll.dll', b'MZ ntdll i386')
    put(prefix, 'drive_c/windows/system32/kernel32.dll', b'MZ kernel32 x64')
    put(prefix, 'drive_c/Program Files/Common Files/note.txt', b'only the seed has this')
    os.makedirs(os.path.join(prefix, 'drive_c/users/prospero/Documents'))
    os.makedirs(os.path.join(prefix, 'dosdevices'))
    os.symlink('../drive_c', os.path.join(prefix, 'dosdevices/c:'))
    os.symlink('/', os.path.join(prefix, 'dosdevices/z:'))
    put(app, 'win/wine/lib/wine/i386-windows/ntdll.dll', b'MZ ntdll i386')
    put(app, 'win/wine/lib/wine/x86_64-windows/kernel32.dll', b'MZ kernel32 x64')
    put(app, 'sce_module/ntdll.dll', b'MZ kernel32 x64')          # outside win/: never a source
    return prefix, app


def index_of(seed):
    with open(os.path.join(seed, 'index.txt'), encoding='utf-8') as f:
        return f.read().split('\n')


def entry(lines, rel):
    for l in lines:
        if l.startswith('f ') and l.split(' ', 3)[3].rsplit(' ', 1)[0] == rel:
            return l
    return None


def personal_redirected_in_user_reg():
    with tempfile.TemporaryDirectory() as d:
        prefix, app = fixture(d)
        seed = os.path.join(d, 'seed')
        r = run(prefix, seed, '--app-root', app)
        check(r.returncode == 0, 'exit 0 (%s)' % r.stderr.strip())
        lines = index_of(seed)
        e = entry(lines, 'user.reg')
        check(e is not None and e.endswith(' obj'), 'user.reg is an object: %r' % e)
        if not e:
            return
        h = e.split(' ')[1]
        with open(os.path.join(seed, 'objects', h), 'rb') as f:
            reg = f.read()
        check(sha(reg) == h and int(e.split(' ')[2]) == len(reg), 'object matches its line')
        check(b'"Personal"="Z:\\\\data\\\\Jk4Speed\\\\Saves"\n' in reg, 'Shell Folders\\Personal redirected')
        check(b'"Personal"=str(2):"Z:\\\\data\\\\Jk4Speed\\\\Saves"\n' in reg, 'User Shell Folders\\Personal redirected')
        check(b'Documents' not in reg, 'no Documents value left')
        check(b'"Personal"="not a shell folder"' in reg, 'another key\'s Personal untouched')
        check(b'"Desktop"=str(2):"%USERPROFILE%\\\\Desktop"' in reg, 'other shell folders untouched')
        with open(os.path.join(prefix, 'user.reg'), 'rb') as f:
            check(f.read() == USER_REG, 'the source prefix is never changed')
        # a prefix without the value: refused, nothing written
        put(prefix, 'user.reg', USER_REG.replace(b'"Personal"=str(2)', b'"Other"=str(2)'))
        seed2 = os.path.join(d, 'seed2')
        r = run(prefix, seed2, '--app-root', app)
        check(r.returncode != 0 and 'Personal' in r.stderr, 'missing User Shell Folders\\Personal refused (%s)' % r.stderr.strip())
        check(not os.path.exists(seed2), 'nothing written when refused')


def app_entries_for_runtime_twins():
    with tempfile.TemporaryDirectory() as d:
        prefix, app = fixture(d)
        seed = os.path.join(d, 'seed')
        r = run(prefix, seed, '--app-root', app)
        check(r.returncode == 0, 'exit 0 (%s)' % r.stderr.strip())
        lines = index_of(seed)
        check(lines[0] == 'JKSEED 1', 'header')
        check(lines[-1] == '', 'ends with a newline')
        e = entry(lines, 'drive_c/windows/syswow64/ntdll.dll')
        check(e == 'f %s 13 drive_c/windows/syswow64/ntdll.dll app:win/wine/lib/wine/i386-windows/ntdll.dll'
              % sha(b'MZ ntdll i386'), 'i386 twin: %r' % e)
        e = entry(lines, 'drive_c/windows/system32/kernel32.dll')
        check(e is not None and e.endswith(' app:win/wine/lib/wine/x86_64-windows/kernel32.dll'), 'x64 twin: %r' % e)
        e = entry(lines, 'drive_c/Program Files/Common Files/note.txt')
        check(e is not None and e.endswith(' obj'), 'seed-only file with spaces is an object: %r' % e)
        objs = sorted(os.listdir(os.path.join(seed, 'objects')))
        check(sha(b'MZ ntdll i386') not in objs, 'no object for a twin')
        check(sha(b'only the seed has this') in objs, 'object for the seed-only file')
        check('l ../drive_c dosdevices/c:' in lines and 'l / dosdevices/z:' in lines, 'links')
        check('d drive_c/users/prospero/Documents' in lines, 'empty folder kept')
        check('d drive_c/Program Files/Common Files' in lines, 'folder with a space')
        # deterministic: a second run gives the same index
        seed2 = os.path.join(d, 'seed2')
        run(prefix, seed2, '--app-root', app)
        check(index_of(seed2) == lines, 'deterministic')
        # an existing output folder is never overwritten
        r = run(prefix, seed, '--app-root', app)
        check(r.returncode != 0, 'existing output refused')


for t in (personal_redirected_in_user_reg, app_entries_for_runtime_twins):
    t()
print('test_make_seed: %d failed' % fails)
sys.exit(1 if fails else 0)

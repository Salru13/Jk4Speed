# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""title/make_gp4.py: the GP4 project PkgTool.Core builds the package from."""
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), 'title', 'make_gp4.py')
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('  FAIL', what)


def run(*args):
    return subprocess.run([sys.executable, '-I', TOOL] + list(args), capture_output=True, text=True)


with tempfile.TemporaryDirectory() as d:
    for rel in ('eboot.bin', 'sce_module/a.prx', 'jk4speed-files/manifest.txt'):
        p = os.path.join(d, 'src', rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(rel.encode())
    lst = os.path.join(d, 'files.tsv')
    with open(lst, 'w') as f:
        for rel in ('sce_module/a.prx', 'eboot.bin', 'jk4speed-files/manifest.txt'):
            f.write('%s\t%s\n' % (rel, os.path.join(d, 'src', rel)))
    out = os.path.join(d, 'p.gp4')
    r = run(out, 'IV0000-JKSP00001_00-JK4SPEED00000001', lst)
    check(r.returncode == 0, 'exit 0 (%s)' % r.stderr.strip())
    root = ET.parse(out).getroot()
    pkg = root.find('volume/package')
    check(pkg.get('content_id') == 'IV0000-JKSP00001_00-JK4SPEED00000001', 'content id')
    targs = [e.get('targ_path') for e in root.find('files')]
    check(targs == sorted(targs) and len(targs) == 3, 'sorted, all files')
    dirs = sorted(e.get('targ_name') for e in root.find('rootdir'))
    check(dirs == ['jk4speed-files', 'sce_module'], 'folders %s' % dirs)
    first = open(out).read()
    run(out, 'IV0000-JKSP00001_00-JK4SPEED00000001', lst)
    check(open(out).read() == first, 'deterministic')
    with open(lst, 'a') as f:
        f.write('../evil\t%s\n' % os.path.join(d, 'src', 'eboot.bin'))
    check(run(out, 'X', lst).returncode != 0, 'a path leaving the package is refused')

print('test_make_gp4: %d failed' % fails)
sys.exit(1 if fails else 0)

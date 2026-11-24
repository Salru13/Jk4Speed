# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Make the Wine prefix seed (jk4speed-files/seed/) that the launcher imports at PATCH (src/jk_seed.c).

    python3 -I launcher/tools/make_seed.py PREFIX OUT_SEED_DIR --app-root UROOT [--saves 'Z:\\data\\Jk4Speed\\Saves']

PREFIX is a working prefix (the bench's ~/nfsmw-bench/kit/library/prefixes/nfsmw); it is only read.
OUT_SEED_DIR must not exist; it gets index.txt and objects/<sha256>:

    JKSEED 1
    d <relpath>                                 every folder, sorted
    l <target> <relpath>                        every symbolic link
    f <sha256> <size> <relpath> app:<path>      byte-identical to <UROOT>/<path> under win/ (the runtime
                                                the package ships anyway: not stored twice)
    f <sha256> <size> <relpath> obj             objects/<sha256>

user.reg is changed in the seed only: the Personal value of Explorer\\Shell Folders and of
Explorer\\User Shell Folders becomes the saves folder, so the game's SHGetFolderPath(CSIDL_PERSONAL)
"\\NFS Most Wanted" lands in /data/Jk4Speed/Saves. Both values must be there exactly once; nothing is
written otherwise. Stdlib only; deterministic output.
"""
import argparse
import hashlib
import os
import re
import shutil
import sys

SAVES = 'Z:\\data\\Jk4Speed\\Saves'
SECTIONS = (b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders]',
            b'[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders]')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def reg_escape(path):
    return path.replace('\\', '\\\\').encode('ascii')


def redirect_personal(reg, saves):
    """user.reg with Personal in both Explorer shell-folder keys set to saves; raises ValueError."""
    lines = reg.split(b'\n')
    value = reg_escape(saves)
    done = {s: 0 for s in SECTIONS}
    section = None
    for i, line in enumerate(lines):
        if line.startswith(b'['):
            section = next((s for s in SECTIONS if line.startswith(s + b' ') or line == s), None)
            continue
        if section is None or not line.startswith(b'"Personal"='):
            continue
        rest = line[len(b'"Personal"='):]
        prefix = b'str(2):' if rest.startswith(b'str(2):') else b''
        if section == SECTIONS[1] and not prefix:
            raise ValueError('User Shell Folders\\Personal is not a str(2) value')
        lines[i] = b'"Personal"=' + prefix + b'"' + value + b'"'
        done[section] += 1
    for s, n in done.items():
        if n != 1:
            name = 'User Shell Folders' if s == SECTIONS[1] else 'Shell Folders'
            raise ValueError('%s\\Personal found %d times in user.reg (want 1)' % (name, n))
    return b'\n'.join(lines)


def runtime_twins(app_root):
    """{size: ['win/...', ...]} for every file under app_root/win, in sorted order (hashed on demand)."""
    win = os.path.join(app_root, 'win')
    if not os.path.isdir(win):
        raise ValueError('no win/ under %s' % app_root)
    by_size = {}
    for base, dirs, files in os.walk(win):
        dirs.sort()
        for name in sorted(files):
            p = os.path.join(base, name)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            rel = os.path.relpath(p, app_root).replace(os.sep, '/')
            if ' ' in rel or '\n' in rel:
                continue
            by_size.setdefault(os.path.getsize(p), []).append(rel)
    return by_size


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('prefix')
    ap.add_argument('out')
    ap.add_argument('--app-root', required=True, help='the package root holding win/ (the 282 payload uroot)')
    ap.add_argument('--saves', default=SAVES)
    a = ap.parse_args(argv)

    def fail(msg):
        print('make_seed: ' + msg, file=sys.stderr)
        return 1

    if os.path.lexists(a.out):
        return fail('%s exists; give a new folder' % a.out)
    if not os.path.isdir(a.prefix) or not os.path.isfile(os.path.join(a.prefix, 'user.reg')):
        return fail('%s is not a Wine prefix (no user.reg)' % a.prefix)
    if not re.fullmatch(r'[A-Za-z]:(\\[^\\"\n]+)+', a.saves):
        return fail('bad --saves %r' % a.saves)
    try:
        by_size = runtime_twins(a.app_root)
    except ValueError as e:
        return fail(str(e))
    twin_sha = {}

    def twin_of(size, digest):
        for rel in by_size.get(size, ()):
            if rel not in twin_sha:
                twin_sha[rel] = sha256_file(os.path.join(a.app_root, rel))
            if twin_sha[rel] == digest:
                return rel
        return None

    dirs, links, files, objects = [], [], [], {}
    for base, dnames, fnames in os.walk(a.prefix):
        dnames.sort()
        keep = []
        for name in dnames:
            p = os.path.join(base, name)
            rel = os.path.relpath(p, a.prefix).replace(os.sep, '/')
            if os.path.islink(p):
                links.append((os.readlink(p), rel))
            else:
                dirs.append(rel)
                keep.append(name)
        dnames[:] = keep
        for name in sorted(fnames):
            p = os.path.join(base, name)
            rel = os.path.relpath(p, a.prefix).replace(os.sep, '/')
            if os.path.islink(p):
                links.append((os.readlink(p), rel))
                continue
            if not os.path.isfile(p):
                return fail('%s is not a file or folder' % rel)
            with open(p, 'rb') as f:
                data = f.read()
            if rel == 'user.reg':
                try:
                    data = redirect_personal(data, a.saves)
                except ValueError as e:
                    return fail(str(e))
            digest = sha256(data)
            src = twin_of(len(data), digest) if rel != 'user.reg' else None
            if src:
                files.append((digest, len(data), rel, 'app:' + src))
            else:
                files.append((digest, len(data), rel, 'obj'))
                objects[digest] = data if rel == 'user.reg' else p
    for target, rel in links:
        if ' ' in target or not target:
            return fail('link %s has a target with a space: %r' % (rel, target))
    for rel in dirs + [r for _, r in links] + [f[2] for f in files]:
        if '\n' in rel or '\r' in rel or rel.startswith('/') or '..' in rel.split('/'):
            return fail('unusable path in the prefix: %r' % rel)

    text = 'JKSEED 1\n'
    text += ''.join('d %s\n' % d for d in sorted(dirs))
    text += ''.join('l %s %s\n' % l for l in sorted(links, key=lambda t: t[1]))
    text += ''.join('f %s %d %s %s\n' % f for f in sorted(files, key=lambda t: t[2]))

    tmp = a.out + '.tmp-make-seed'
    if os.path.lexists(tmp):
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'objects'))
    for digest, src in sorted(objects.items()):
        dst = os.path.join(tmp, 'objects', digest)
        if isinstance(src, bytes):
            with open(dst, 'wb') as f:
                f.write(src)
        else:
            shutil.copyfile(src, dst)
        if sha256_file(dst) != digest:
            shutil.rmtree(tmp)
            return fail('object %s changed while it was copied' % digest)
    with open(os.path.join(tmp, 'index.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.rename(tmp, a.out)
    obj_bytes = sum(f[1] for f in files if f[3] == 'obj')
    app_n = sum(1 for f in files if f[3] != 'obj')
    print('make_seed: %d folders, %d links, %d files (%d runtime twins, %d objects %d bytes), index %s -> %s'
          % (len(dirs), len(links), len(files), app_n, len(objects), obj_bytes,
             sha256(text.encode('utf-8')), a.out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Write jk4speed-files/manifest.txt for the launcher's patcher (spec 8.1).

    python3 -I launcher/tools/make_manifest.py FILES_DIR
        [--exe speed.exe:<sha256>]          default: speed.exe v1.3
        [--require-list FILE]               game paths that must exist, one per line
        [--remove-list FILE]                conflicting third-party files, one per line
        [--config RELPATH]...               files installed only when missing
        [--seed NAME.reg]                   registry pre-seed (goes to <data_root>/registry)
        [--accept RELPATH:<sha256>]...      a second accepted hash for a file line

Every other file under FILES_DIR becomes a "file" line. r4: the launcher composes
jk4speed-dev.ini and nfsmw.profile itself (from jk4speed.ini.base and nfsmw.profile.base, read
from FILES_DIR, never installed) and the native DXVK server ships in sce_module/, so there are no
ini, "profile" or "server" lines: a packaged jk4speed-dev.ini or dxvknative.prx is refused. seed/
(the Wine prefix seed), deltas/, xenon/ and about/ are read by the launcher, never installed. Paths are checked
against the same allow-list as the launcher (jk_manifest.c): a package can
never name a game file, and never one of the pinned mod files (HD road/HUD,
X360 Stuff Pack): those come only from the player's USB drop folder. Nothing
is written when a check fails. Stdlib only; deterministic output.
"""
import argparse
import hashlib
import os
import re
import sys

V13 = '80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c'
TOP_NAMES = {n.lower() for n in ('d3d9.dll', 'dinput8.dll', 'dsound.dll', 'Xidi.32.dll', 'Xidi.ini', 'dxvk.conf')}
# r4: read by the launcher (jk_config.c composes from them), never installed
BASE_FILES = ('jk4speed.ini.base', 'nfsmw.profile.base')
SCRIPT_EXTS = ('.asi', '.cfg', '.ini', '.tpk')
# The pinned mod files (launcher/src/jk_mods.c): HD road/HUD, our builds over game data and the
# X360 Stuff Pack files. They come only from the player's USB drop folder (<usb>/Jk4Speed/mods/);
# a package never carries them.
MOD_FILES = {
    'TRACKS/STREAML2RA.BUN', 'GLOBAL/HUDTEXRACE.BIN', 'GLOBAL/InGameA.bun', 'GLOBAL/HUDS_Custom_00.bin',
    'GLOBAL/attributes.bin', 'GLOBAL/GlobalB.lzc', 'scripts/X360Stuff.ini', 'scripts/X360Stuff.asi',
    'scripts/NFSMW_XenonEffects.asi', 'scripts/NFSMW_XenonEffects.ini', 'scripts/XenonEffects.tpk',
} | {'FX/COMPILED/%s.fx.o' % n for n in (
    'car', 'carnormalmap', 'debugPoly', 'glassreflectshader', 'grass', 'hdr', 'particles', 'shadowscreenfilter',
    'shadowvolume', 'sky', 'standard', 'trees', 'visualtreatment', 'waterseashader', 'world', 'worldbone',
    'worldnormalmap', 'worldreflect')}
ORDER = ['exe', 'require', 'file', 'config', 'remove', 'seed', 'accept']


def safe_relative(p):
    if not p or p.startswith('/') or len(p) >= 160 or re.search(r'[\\:\s]', p) or any(ord(c) > 0x7E for c in p):
        return False
    return all(seg not in ('', '.', '..') for seg in p.split('/'))


def allowed(kind, p):
    if not safe_relative(p):
        return False
    if kind == 'require':
        return True
    if kind == 'exe':
        return '/' not in p and p.lower().endswith('.exe')
    if kind == 'seed':
        return '/' not in p and p.lower().endswith('.reg')
    if p in MOD_FILES:
        return False
    if p.lower() in TOP_NAMES:
        return True
    if p[:8].lower() != 'scripts/':
        return False
    name = p[8:]
    return bool(name) and '/' not in name and name.lower().endswith(SCRIPT_EXTS)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 16), b''):
            h.update(block)
    return h.hexdigest()


def read_list(path):
    if not path:
        return []
    with open(path, encoding='utf-8') as f:
        return [l.strip() for l in f if l.strip() and not l.strip().startswith('#')]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('files_dir')
    ap.add_argument('--exe', default='speed.exe:' + V13)
    ap.add_argument('--require-list')
    ap.add_argument('--remove-list')
    ap.add_argument('--config', action='append', default=[])
    ap.add_argument('--seed')
    ap.add_argument('--accept', action='append', default=[])
    a = ap.parse_args(argv)

    errors, lines = [], []
    exe, _, exe_sha = a.exe.partition(':')
    if not allowed('exe', exe) or not re.fullmatch(r'[0-9a-f]{64}', exe_sha):
        errors.append('bad --exe %r (want NAME.exe:<64 hex>)' % a.exe)
    lines.append(('exe', exe, 'exe %s %s' % (exe, exe_sha)))
    for p in read_list(a.require_list):
        if not allowed('require', p):
            errors.append('require path not allowed: %s' % p)
        lines.append(('require', p, 'require %s' % p))

    found = []
    for base, dirs, files in os.walk(a.files_dir):
        dirs.sort()
        for name in sorted(files):
            rel = os.path.relpath(os.path.join(base, name), a.files_dir).replace(os.sep, '/')
            if rel in ('manifest.txt', 'manifest.txt.tmp') or rel in BASE_FILES or \
                    rel.startswith(('deltas/', 'xenon/', 'about/', 'seed/')):
                continue                       # read by the launcher (deltas, Xenon Effects, credits, config bases, prefix seed)
            found.append(rel)
    configs = set(a.config)
    for rel in found:
        full = os.path.join(a.files_dir, rel)
        size = os.path.getsize(full)
        if rel == a.seed:
            kind = 'seed'
        elif rel in configs:
            kind = 'config'
        else:
            kind = 'file'
        if rel in MOD_FILES:
            errors.append('%s is a mod file: it comes from the player\'s mods folder, never from the package' % rel)
            continue
        if not allowed(kind, rel):
            errors.append('%s path not allowed (not one of our file names): %s' % (kind, rel))
            continue
        lines.append((kind, rel, '%s %s %s %d' % (kind, rel, sha256(full), size)))
    for p in sorted(configs - set(found)):
        errors.append('--config %s is not in %s' % (p, a.files_dir))
    if a.seed and a.seed not in found:
        errors.append('--seed %s is not in %s' % (a.seed, a.files_dir))
    file_paths = {t[1] for t in lines if t[0] == 'file'}
    for spec in a.accept:
        p, _, h = spec.partition(':')
        if not re.fullmatch(r'[0-9a-f]{64}', h):
            errors.append('bad --accept %r (want RELPATH:<64 hex>)' % spec)
        elif p not in file_paths:
            errors.append('--accept %s has no file line' % p)
        else:
            lines.append(('accept', p, 'accept %s %s' % (p, h)))
    for p in read_list(a.remove_list):
        if not allowed('remove', p):
            errors.append('remove path not allowed: %s' % p)
        lines.append(('remove', p, 'remove %s' % p))

    if errors:
        for e in errors:
            print('make_manifest: ' + e, file=sys.stderr)
        return 1
    lines.sort(key=lambda t: (ORDER.index(t[0]), t[1]))
    text = '# Jk4Speed package manifest (launcher/tools/make_manifest.py). Do not edit by hand.\n'
    text += ''.join(t[2] + '\n' for t in lines)
    out = os.path.join(a.files_dir, 'manifest.txt')
    with open(out + '.tmp', 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.replace(out + '.tmp', out)
    print('make_manifest: %d entries -> %s' % (len(lines), out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

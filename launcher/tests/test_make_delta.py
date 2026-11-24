# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""tools/make_delta.py: binary deltas that hold only our changes.

A delta rebuilds a target from the player's own source files. It must not be
able to rebuild a source on its own: its literal bytes never contain a block
of a source, and without the sources it cannot be applied at all.
"""
import hashlib
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(os.path.dirname(HERE), 'tools', 'make_delta.py')
FIXTURE = os.path.join(HERE, 'fixtures')
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'tools'))
import make_delta  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('  FAIL', what)


def rnd(n, seed):
    r = random.Random(seed)
    return bytes(r.getrandbits(8) for _ in range(n))


def put(path, data):
    with open(path, 'wb') as f:
        f.write(data)


def run(*args):
    return subprocess.run([sys.executable, '-I', TOOL] + list(args), capture_output=True, text=True)


def scenario():
    """A game file (source A), a pack file (source B), and our build: mostly
    A, a block of B, a few new bytes in place, one header changed."""
    a = rnd(200_000, 1)
    b = rnd(50_000, 2)
    ours = rnd(3_000, 3)
    target = bytearray(a)
    target[10_000:13_000] = ours                      # our new texture, same size
    target[100_000:120_000] = b[5_000:25_000]         # a piece of the pack file
    target[0:16] = b'JK4SPEED-HEADER!'
    target += b'appended tail of ours'
    return a, b, bytes(target), ours


with tempfile.TemporaryDirectory() as d:
    a, b, target, ours = scenario()
    pa, pb, pt, pd = (os.path.join(d, n) for n in ('a.bin', 'b.bin', 't.bin', 't.jkd'))
    put(pa, a); put(pb, b); put(pt, target)

    r = run('build', pt, pd, pa, pb)
    check(r.returncode == 0, 'build exit 0 (%s)' % r.stderr.strip())
    delta = open(pd, 'rb').read()
    info = make_delta.read_delta(delta)

    # round trip with the sources
    out = os.path.join(d, 'out.bin')
    r = run('apply', pd, out, pa, pb)
    check(r.returncode == 0 and open(out, 'rb').read() == target, 'apply rebuilds the target')

    # small: only our changes
    lit = sum(len(op[1]) for op in info['ops'] if op[0] == 'add')
    check(lit <= len(ours) + 16 + 64 * 4 + len(b'appended tail of ours'), 'literal bytes are only ours (%d)' % lit)
    check(len(delta) < len(target) // 20, 'delta is small (%d of %d)' % (len(delta), len(target)))

    # cannot rebuild a source on its own
    literals = b''.join(op[1] for op in info['ops'] if op[0] == 'add')
    for name, src in (('a', a), ('b', b)):
        blocks = {src[i:i + 64] for i in range(0, len(src) - 63, 64)}
        hit = any(literals[i:i + 64] in blocks for i in range(0, max(0, len(literals) - 63)))
        check(not hit, 'no 64-byte block of source %s in the literals' % name)
        check(len(literals) < len(src) // 10, 'literals far smaller than source %s' % name)
    check(make_delta.source_free(delta, [pa, pb]), 'source_free() agrees')

    # without its sources, or with the wrong one, nothing is written
    os.remove(out)
    r = run('apply', pd, out, pb, pa)                 # swapped order
    check(r.returncode != 0 and not os.path.exists(out), 'wrong source refused')
    r = run('apply', pd, out, pa)
    check(r.returncode != 0 and not os.path.exists(out), 'missing source refused')
    r = run('apply', pd, out, pa, os.path.join(d, 'nope'))
    check(r.returncode != 0, 'absent source file refused')

    # a delta whose literals would carry a source block is refused at build time
    leak = bytes(a[:4096]) + b'x'                      # target = source copy, no source given for it
    pl = os.path.join(d, 'leak.bin')
    put(pl, leak)
    r = run('build', pl, os.path.join(d, 'leak.jkd'), pb, '--guard', pa)
    check(r.returncode != 0 and 'source block' in r.stderr, 'leak of a guarded source refused (%s)' % r.stderr.strip())

    # deterministic
    r = run('build', pt, pd + '2', pa, pb)
    check(open(pd + '2', 'rb').read() == delta, 'deterministic')

# JDLZ: the encoder decodes with the game-exact decoder (tools/mods/bun.py)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', 'mods'))
try:
    import bun
except ImportError:
    bun = None
if bun:
    r = random.Random(11)
    for n in (0, 1, 2, 3, 7, 8, 9, 64, 1000, 4099, 20000):
        data = bytes(r.choice(b'ab\x00\xff') for _ in range(n)) + rnd(n // 3, n)
        check(bun.jdlz_decompress(make_delta.jdlz(data)) == data, 'jdlz round trip %d' % n)

# a JDLZ op: our container around a compressed copy of a source range
with tempfile.TemporaryDirectory() as d:
    a, b = rnd(9000, 21), rnd(4000, 22)
    record = a[100:5100] + b'our record tail'
    target = b'HEADER' + make_delta.jdlz(record) + bytes(10)
    ops = [('add', b'HEADER'), ('jdlz', [('copy', 0, 100, 5000), ('add', b'our record tail')]), ('add', bytes(10))]
    out = os.path.join(d, 'j.jkd')
    delta, keep = make_delta.finish(target, [a, b], ops, out)
    check(keep == [0], 'unused source b dropped (%s)' % keep)
    check(make_delta.apply(delta, [a]) == target, 'jdlz op rebuilds the target')
    check(len(delta) < 400, 'jdlz delta holds no pixels (%d bytes)' % len(delta))
    check(make_delta.source_free(delta, []), 'source_free with no guards')
    bad_guard = os.path.join(d, 'g.bin')
    put(bad_guard, a)
    check(make_delta.source_free(delta, [bad_guard]), 'the jdlz delta has no block of the source')

# the C applier's fixtures are what this tool writes
fj = os.path.join(FIXTURE, 'delta', 'jdlz.jkd')
if os.path.exists(fj):
    a = open(os.path.join(FIXTURE, 'delta', 'a.bin'), 'rb').read()
    want = open(os.path.join(FIXTURE, 'delta', 'jdlz-target.bin'), 'rb').read()
    check(make_delta.apply(open(fj, 'rb').read(), [a]) == want, 'jdlz fixture matches its target')
fx = os.path.join(FIXTURE, 'delta')
if os.path.isdir(fx):
    want = open(os.path.join(fx, 'target.bin'), 'rb').read()
    info = make_delta.read_delta(open(os.path.join(fx, 'fixture.jkd'), 'rb').read())
    check(info['target_sha'] == hashlib.sha256(want).hexdigest(), 'fixture matches its target')

# the catalog names a target for every path and every subset of the three mods
AUTHOR = os.path.join(os.path.dirname(HERE), 'title', 'author_deltas.py')
INC = os.path.join(os.path.dirname(HERE), 'src', 'jk_modset_catalog.inc')
r = subprocess.run([sys.executable, '-I', AUTHOR, '--verify-table', INC], capture_output=True, text=True)
check(r.returncode == 0, 'verify-table: every path x 8 subsets (%s)' % r.stdout.strip().splitlines()[-1:])
check(len([l for l in r.stdout.splitlines() if not l.startswith('verify-table')]) % 8 == 0, 'verify-table: 8 lines per path')
with tempfile.TemporaryDirectory() as d:
    rec = '    { "%s", 4LL, JK_MS_COPY, "%s", NULL },\n' % ('a' * 64, 'a' * 64)
    full = '    { "P", 1, { "=", "%s", "=", "%s", "=", "%s", "=", "%s" } },\n' % (('a' * 64,) * 4)
    short = '    { "P", 1, { "%s", NULL, NULL } },\n' % ('a' * 64)
    for name, row, want in (('full', full, 0), ('r3', short, 1),
                            ('no-recipe', full.replace('a' * 64, 'b' * 64, 1), 1)):
        p = os.path.join(d, name + '.inc')
        put(p, (rec + row).encode())
        r = subprocess.run([sys.executable, '-I', AUTHOR, '--verify-table', p], capture_output=True, text=True)
        check(r.returncode == want, 'verify-table %s exits %d (%d)' % (name, want, r.returncode))

print('test_make_delta: %d failed' % fails)
sys.exit(1 if fails else 0)

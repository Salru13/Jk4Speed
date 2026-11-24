# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Binary deltas (JKD1) that rebuild a Jk4Speed file from the player's own files.

    python3 -I make_delta.py build TARGET OUT.jkd SOURCE... [--guard FILE]...
    python3 -I make_delta.py apply DELTA OUT SOURCE...
    python3 -I make_delta.py info DELTA [SOURCE...]

A delta is a list of COPY (source, offset, length), ADD (literal bytes) and
JDLZ (compress the bytes of nested COPY/ADD parts with the game's JDLZ
format) operations, plus the SHA-256 and size of the target and of every
source. The launcher (src/jk_delta.c) applies it on the console to the
player's files (the game, the X360 Stuff Pack and the mod authors' own
releases) and checks every hash before and after.

What a delta may hold: only the bytes that are in none of its sources, i.e.
our changes. `build` refuses to write a delta whose literal bytes contain a
64-byte block of any source or --guard file, so a delta can never carry a
copy of game, pack or mod-author data, and it cannot be applied without its
sources. Sources the operations never use are dropped from the header.

Format (little endian):
    "JKD1" u32 nsrc | target sha256[32] u64 size | nsrc x (sha256[32] u64 size)
    ops: 0x01 u8 src u64 offset u32 len      COPY
         0x02 u32 len bytes                  ADD
         0x03 u32 nparts parts...            JDLZ of the parts' bytes (parts: COPY/ADD)
         0x00                                END
Stdlib only; deterministic.
"""
import hashlib
import struct
import sys

B = 64                       # block size of the source index
INDEX_MAX = 64 << 20         # sources above this are matched in place only
MAGIC = b'JKD1'
MAX_SRC = 255


def sha(data):
    return hashlib.sha256(data).hexdigest()


def jdlz(data):
    """The game's JDLZ, as evidence/codex-upscaled-hud/gauges/indexed/build.py
    writes it (the JDLZ-safe version with the trailing refill bytes; see
    docs/status/2026-11-23-jdlz-crash.md). src/jk_delta.c has the same
    encoder; both must give the same bytes."""
    body = bytearray()
    history = {}
    position = literals = matches = 0
    first = second = 0
    refill = False
    while position < len(data):
        if literals % 8 == 0:
            first = len(body)
            body.append(0)
        if matches % 8 == 0 and (position == 0 or refill):
            second = len(body)
            body.append(0)
        refill = False
        prefix = data[position:position + 3]
        distance = position - history.get(prefix, -4096)
        length = 1
        if distance <= 2064 and len(prefix) == 3:
            limit = min(len(data) - position, 4098 if distance <= 16 else 34)
            length = 3
            while length < limit and data[position + length] == data[position + length - distance]:
                length += 1
            body[first] |= 1 << (literals % 8)
            count = length - 3
            if distance <= 16:
                body[second] |= 1 << (matches % 8)
                body.extend((((count >> 4) & 0xf0) | (distance - 1), count & 255))
            else:
                span = distance - 17
                body.extend((((span >> 3) & 0xe0) | count, span & 255))
            matches += 1
            refill = matches % 8 == 0
        else:
            body.append(data[position])
        for i in range(position, position + length):
            history[data[i:i + 3]] = i
        position += length
        literals += 1
    if literals % 8 == 0:
        body.append(0)
    if refill:
        body.append(0)
    return b"JDLZ\x02\x10\0\0" + struct.pack("<II", len(data), len(body) + 16) + bytes(body)


def make_index(sources):
    index = {}
    for si, src in enumerate(sources):
        if len(src) > INDEX_MAX:          # a 532 MB road: in-place matching only (memory)
            continue
        for off in range(0, len(src) - B + 1, B):
            index.setdefault(src[off:off + B], (si, off))
    return index


def build_ops(target, sources, index=None, in_place=True):
    """Greedy rsync-style matching: aligned B-byte blocks of every source
    are indexed; at each target position the same offset in source 0 is
    tried first (in-place edits), then the index. Matches extend forward and
    backward into pending literals."""
    if index is None:
        index = make_index(sources)
    ops, lit_start, i, n = [], 0, 0, len(target)

    def flush(upto):
        nonlocal lit_start
        if upto > lit_start:
            ops.append(('add', target[lit_start:upto]))
        lit_start = upto

    s0 = sources[0] if sources and in_place else b''
    while i < n:
        match = None
        if i + B <= n and i + B <= len(s0) and s0[i:i + B] == target[i:i + B]:
            match = (0, i)
        elif i + B <= n:
            match = index.get(target[i:i + B])
        if not match:
            i += 1
            continue
        si, off = match
        src = sources[si]
        back = 0
        while i - back > lit_start and off - back > 0 and src[off - back - 1] == target[i - back - 1]:
            back += 1
        start, soff = i - back, off - back
        length = back + B
        while start + length + B <= n and soff + length + B <= len(src) and \
                src[soff + length:soff + length + B] == target[start + length:start + length + B]:
            length += B
        while start + length < n and soff + length < len(src) and src[soff + length] == target[start + length]:
            length += 1
        flush(start)
        if ops and ops[-1][0] == 'copy' and ops[-1][1] == si and ops[-1][2] + ops[-1][3] == soff:
            ops[-1] = ('copy', si, ops[-1][2], ops[-1][3] + length)
        else:
            ops.append(('copy', si, soff, length))
        i = start + length
        lit_start = i
    flush(n)
    return ops


def prune(sources, ops):
    """Drop the sources no operation uses; renumber."""
    used = set()

    def walk(seq):
        for op in seq:
            if op[0] == 'copy':
                used.add(op[1])
            elif op[0] == 'jdlz':
                walk(op[1])
    walk(ops)
    keep = sorted(used)
    remap = {old: new for new, old in enumerate(keep)}

    def re(seq):
        out = []
        for op in seq:
            if op[0] == 'copy':
                out.append(('copy', remap[op[1]], op[2], op[3]))
            elif op[0] == 'jdlz':
                out.append(('jdlz', re(op[1])))
            else:
                out.append(op)
        return out
    return [sources[k] for k in keep], re(ops), keep


def _encode_ops(ops, out):
    for op in ops:
        if op[0] == 'copy':
            _, si, off, length = op
            while length:
                part = min(length, 0xFFFFFFFF)
                out += struct.pack('<BBQI', 1, si, off, part)
                off += part
                length -= part
        elif op[0] == 'add':
            data = op[1]
            for k in range(0, len(data), 0xFFFFFFFF):
                chunk = data[k:k + 0xFFFFFFFF]
                out += struct.pack('<BI', 2, len(chunk)) + chunk
        else:
            parts = op[1]
            if any(p[0] == 'jdlz' for p in parts):
                raise ValueError('nested JDLZ')
            out += struct.pack('<BI', 3, len(parts))
            _encode_ops(parts, out)


def encode(target, sources, ops):
    if len(sources) > MAX_SRC:
        raise ValueError('too many sources')
    out = bytearray(MAGIC + struct.pack('<I', len(sources)))
    out += bytes.fromhex(sha(target)) + struct.pack('<Q', len(target))
    for s in sources:
        out += bytes.fromhex(sha(s)) + struct.pack('<Q', len(s))
    _encode_ops(ops, out)
    out += b'\x00'
    return bytes(out)


def _read_op(data, pos, kind):
    if kind == 1:
        si, off, length = struct.unpack_from('<BQI', data, pos)
        return ('copy', si, off, length), pos + 13
    if kind == 2:
        length = struct.unpack_from('<I', data, pos)[0]
        return ('add', data[pos + 4:pos + 4 + length]), pos + 4 + length
    raise ValueError('bad op')


def read_delta(data):
    if data[:4] != MAGIC:
        raise ValueError('not a JKD1 delta')
    nsrc = struct.unpack_from('<I', data, 4)[0]
    if not 1 <= nsrc <= MAX_SRC:
        raise ValueError('bad source count')
    pos = 8
    tsha = data[pos:pos + 32].hex(); tsize = struct.unpack_from('<Q', data, pos + 32)[0]; pos += 40
    srcs = []
    for _ in range(nsrc):
        srcs.append((data[pos:pos + 32].hex(), struct.unpack_from('<Q', data, pos + 32)[0])); pos += 40
    ops = []
    while True:
        kind = data[pos]; pos += 1
        if kind == 0:
            break
        if kind == 3:
            nparts = struct.unpack_from('<I', data, pos)[0]; pos += 4
            parts = []
            for _ in range(nparts):
                k = data[pos]; pos += 1
                op, pos = _read_op(data, pos, k)
                parts.append(op)
            ops.append(('jdlz', parts))
        else:
            op, pos = _read_op(data, pos, kind)
            ops.append(op)
    if pos != len(data):
        raise ValueError('trailing bytes')
    return {'target_sha': tsha, 'target_size': tsize, 'sources': srcs, 'ops': ops}


def _run(ops, sources, out):
    for op in ops:
        if op[0] == 'copy':
            _, si, off, length = op
            out += sources[si][off:off + length]
        elif op[0] == 'add':
            out += op[1]
        else:
            inner = bytearray()
            _run(op[1], sources, inner)
            out += jdlz(bytes(inner))


def apply(delta, sources):
    info = read_delta(delta)
    if len(sources) != len(info['sources']):
        raise ValueError('needs %d sources' % len(info['sources']))
    for (want, size), s in zip(info['sources'], sources):
        if len(s) != size or sha(s) != want:
            raise ValueError('a source is not the expected file')
    out = bytearray()
    _run(info['ops'], sources, out)
    if sha(out) != info['target_sha']:
        raise ValueError('result does not match')
    return bytes(out)


def literals(delta):
    """Every ADD's bytes, the ones inside JDLZ parts included."""
    found = []

    def walk(seq):
        for op in seq:
            if op[0] == 'add':
                found.append(op[1])
            elif op[0] == 'jdlz':
                walk(op[1])
    walk(read_delta(delta)['ops'])
    return found


def literal_blocks_from(delta, guards):
    """Offsets (in the literal stream) where a 64-byte block of a guard file
    appears inside one ADD operation. Each ADD is checked on its own: two
    literals are never adjacent in the target, so a window across them is not
    a copy of anything."""
    adds = literals(delta)
    hits = []
    for g in guards:
        # hashes of the aligned blocks (ints: a 532 MB file fits in memory); a
        # hash hit is checked byte for byte
        blocks = {}
        for i in range(0, len(g) - B + 1, B):
            blocks.setdefault(hash(g[i:i + B]), i)
        base = 0
        for lit in adds:
            for i in range(0, len(lit) - B + 1):
                off = blocks.get(hash(lit[i:i + B]))
                if off is not None and g[off:off + B] == lit[i:i + B]:
                    hits.append(base + i)
                    break
            if hits:
                break
            base += len(lit)
    return hits


def source_free(delta, guard_paths):
    return not literal_blocks_from(delta, [open(p, 'rb').read() for p in guard_paths])


def finish(target, sources, ops, out_path, guards=(), label=''):
    """Prune, encode, round-trip, guard-check and write. Returns (delta, kept source indexes)."""
    kept_sources, ops, keep = prune(sources, ops)
    probe = encode(target, kept_sources or [b''], ops)       # for the literal check only
    hits = literal_blocks_from(probe, list(sources) + list(guards))
    if hits:
        raise ValueError('the literals would carry a source block (at %d); refused' % hits[0])
    if not kept_sources:
        raise ValueError('no source is used: the delta would be a full copy; refused')
    delta = encode(target, kept_sources, ops)
    if apply(delta, kept_sources) != target:
        raise ValueError('round trip failed')
    with open(out_path, 'wb') as f:
        f.write(delta)
    lit = sum(len(x) for x in literals(delta))
    print('make_delta: %s%s %d bytes (literal %d, target %d, %d sources)' %
          (label, out_path, len(delta), lit, len(target), len(kept_sources)))
    return delta, keep


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, args = argv[0], argv[1:]
    guards = []
    while '--guard' in args:
        k = args.index('--guard')
        guards.append(args[k + 1])
        del args[k:k + 2]
    if cmd == 'build' and len(args) >= 3:
        target = open(args[0], 'rb').read()
        sources = [open(p, 'rb').read() for p in args[2:]]
        try:
            _, keep = finish(target, sources, build_ops(target, sources), args[1],
                             [open(p, 'rb').read() for p in guards])
        except ValueError as e:
            print('make_delta: %s' % e, file=sys.stderr)
            return 1
        print('make_delta: sources used: %s' % ' '.join(args[2 + k] for k in keep))
        return 0
    if cmd == 'apply' and len(args) >= 3:
        try:
            sources = [open(p, 'rb').read() for p in args[2:]]
            out = apply(open(args[0], 'rb').read(), sources)
        except (OSError, ValueError) as e:
            print('make_delta: %s' % e, file=sys.stderr)
            return 1
        with open(args[1], 'wb') as f:
            f.write(out)
        return 0
    if cmd == 'info' and len(args) >= 1:
        delta = open(args[0], 'rb').read()
        info = read_delta(delta)
        lit = sum(len(x) for x in literals(delta))
        print('target %s %d; delta %d bytes; literal %d bytes; %d ops' %
              (info['target_sha'], info['target_size'], len(delta), lit, len(info['ops'])))
        for s, size in info['sources']:
            print('source %s %d' % (s, size))
        if len(args) > 1:
            print('source blocks in literals: %s' % ('none' if source_free(delta, args[1:]) else 'FOUND'))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

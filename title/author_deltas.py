# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Build the mod set: the JKD1 deltas the launcher applies at PATCH and the
catalog it uses to plan them (launcher/src/jk_modset_catalog.inc).

Three mods, dropped by the player as released (still zipped) in Jk4Speed/:

  X  Xbox 360 Stuff Pack v4.2 (its download, any zip/7z holding the pack's
     files; found by entry name + size, each entry checked by SHA-256)
  H  Upscaled HUD (LunicAura106), the 7z, checked whole by SHA-256
  R  HD Road Textures (N4SRazer), the ZIP, checked whole by SHA-256

Any subset works (at[set], bits X=1, H=2, R=4). With X, H and R build on the
X files; without X, H0 and R0 are the HUD and the road over the untouched
game files (the HUD's own script skips its 360v4+ variants then). The H and
R builders must first give back the r3 targets over X. Every delta's sources are the player's own game files,
the archives' entries (by hash) or an intermediate the launcher made (the X
road). Nothing from the pack, the HUD or the road is in a delta: the
literals are checked against every 64-byte aligned block of every pack,
HUD and road file, and any hit fails the build.

    python3 -I author_deltas.py OUT_DIR [CATALOG_INC]
    python3 -I author_deltas.py --verify-table [CATALOG_INC]   (every path x 8 subsets named)

Inputs are local, git-ignored copies, each pinned by hash.
"""
import csv
import hashlib
import io
import json
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'tools'))
import make_delta as md  # noqa: E402

EV = os.environ.get('JK_EVIDENCE', '' + os.path.expanduser('~/jk4speed-work/evidence') + '')
PACK = os.environ.get('JK_PACK', '' + os.path.expanduser('~/nfsmw-mods') + '/x360-v4.2-complex')      # the pack, as downloaded
PACK_TSV = os.environ.get('JK_PACK_TSV', '' + os.path.expanduser('~/nfsmw-mods') + '/manifest-x360-v4.2-complex.tsv')
PC = os.environ.get('JK_PC_GAME', '/mnt/c/NFS Most Wanted')                                  # untouched v1.3 game
XDIR = os.environ.get('JK_PC_MODS', '' + os.path.expanduser('~/nfsmw-mods') + '/NFSMW-X360')            # Xenon Effects files
HUD_DIR = os.path.join(EV, 'codex-upscaled-hud/source')            # the Upscaled HUD 7z, extracted as released
HUD_7Z = os.path.join(EV, 'codex-upscaled-hud/official-hud.archive')
ROAD_ZIP = os.path.join(EV, 'road-visual/hd-road.archive')         # N4SRazer's ZIP as downloaded

HUD_SHA = '61d793dea108eafff9a57484e86b2e2c6d6a888a537f9de8fac19c436f189660'
ROAD_SHA = '7857e85120564227fe90ec0667cc9414c90136e1937431fe59bd321e0bbce4e9'
BAD_HUDS = '08b9d1ef4847e1092d8bbfcd4aa8181ce4532438ba042b420ab2661f36d6706d'   # the JDLZ crash file


def sha(b):
    return hashlib.sha256(b).hexdigest()


def read(path, want):
    data = open(path, 'rb').read()
    got = sha(data)
    if not got.startswith(want):
        sys.exit('author_deltas: %s is %s, want %s' % (path, got[:16], want))
    return data


def file_sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


class Src:
    """A delta source and where the launcher finds it."""
    def __init__(self, data, kind, name):
        self.data, self.kind, self.name = data, kind, name
        self.sha, self.size = sha(data), len(data)


def game(path, want):
    return Src(read(os.path.join(PC, path), want), 'GAME', path)


def huds_ops(target, original, dds):
    """HUDS_Custom_00.bin: the game's header with our index rows (literal,
    small), then per texture a JDLZ op over (author DDS pixel range + our TPK
    records); the compressed bytes are made on the console."""
    index = md.make_index(dds)
    ops, pos = [], 0
    blocks = []

    def segment(a, b):            # target[a:b] against the original at the same offsets
        return [('copy', 0, p[2] + a, p[3]) if p[0] == 'copy' else p
                for p in md.build_ops(target[a:b], [original[a:]])]
    for off in range(0, len(target), 64):
        if target[off:off + 4] == b'JDLZ':
            raw, total = struct.unpack_from('<II', target, off + 8)
            blocks.append((off, total, raw))
    for off, total, raw in blocks:
        if off > pos:
            ops += segment(pos, off)
        rec = bun.jdlz_decompress(target[off:off + total])
        if md.jdlz(rec) != target[off:off + total]:
            raise SystemExit('author_deltas: our JDLZ does not reproduce the block at %d' % off)
        parts = md.build_ops(rec, dds, index=index, in_place=False)
        ops.append(('jdlz', [(p[0], p[1] + 1, p[2], p[3]) if p[0] == 'copy' else p for p in parts]))
        pos = off + total
    if pos < len(target):
        ops += segment(pos, len(target))
    return ops


def hud_build(base, groups):
    """The Upscaled HUD's own install step (its userstart.end): bind its DDS
    into the named TPK collections, every texture checked in the result.
    groups: [(collection, [(stem, dds bytes)])], later groups win."""
    bundle = bun.BunFile(base)
    want = {}
    for group, dds in groups:
        bun.bind_textures(bundle.tpk(group), dds)
        want.update({(group, stem): raw for stem, raw in dds})
    data = bundle.build()
    baked = bun.BunFile(data)
    for (group, stem), raw in want.items():
        tex, d = baked.tpk(group).find(bun.bin_hash(stem)), bun.read_dds(raw)
        if tex is None or tex.data != d['data'] or tex.get('Width') != d['width'] or tex.get('Height') != d['height']:
            sys.exit('author_deltas: HUD texture %s/%s not in the result' % (group, stem))
    return data


def road_build(base, road):
    """N4SRazer's road textures over a road (the same 25 pixel ranges in the
    untouched and the X road): each DDS is found by its section/index name,
    its size, format and mip chain checked against the texture it replaces,
    and only that texture's pixels are overwritten. Same size as the base."""
    import re
    offsets = [x.start() for x in re.finditer(re.escape(bytes.fromhex('000030b3')), base) if x.start() % 16 == 0]
    patches = []
    for filename, raw in road.items():
        if not filename.endswith('.dds'):
            continue
        a, b, stem = re.fullmatch(r'.*/([0-9]+)-([0-9]+) (.*)\.dds', filename).groups()
        off = offsets[int(a) - 1]
        size = struct.unpack_from('<I', base, off + 4)[0] + 8
        t = bun.BunFile(base[off:off + size]).tpks[0]
        tex = t.textures[int(b) - 1]
        if tex.get('name') != stem:
            matches = [x for x in t.textures if x.get('name') == stem]
            if len(matches) != 1:
                sys.exit('author_deltas: road texture %s: %d matches' % (filename, len(matches)))
            tex, = matches
        d, fmt = bun.read_dds(raw), raw[84:88]
        if (tex.get('Width'), tex.get('Height'), tex.get('Compression'), tex.get('PaletteSize')) != \
                (d['width'], d['height'], d['compression'], 0) or fmt not in (b'DXT1', b'DXT3', b'DXT5'):
            sys.exit('author_deltas: road texture %s does not fit the road' % filename)
        block = 8 if fmt == b'DXT1' else 16
        n = tex.get('Mipmaps')
        mips = sum(max(1, (max(1, d['width'] >> i) + 3) // 4) * max(1, (max(1, d['height'] >> i) + 3) // 4) * block
                   for i in range(n))
        if n > d['mipmaps'] or mips != len(tex.data):
            sys.exit('author_deltas: road texture %s mip chain does not fit' % filename)
        group, = [c for c in t.chunk.children if c.id == bun.ID_TPK_DATA]
        pixel, = [c for c in group.children if c.id == bun.ID_DATA2]
        addr = off + pixel.data_off + 0x78 + tex.get('Offset')
        fresh = d['data'][:len(tex.data)]
        if base[addr:addr + len(fresh)] != tex.data or fresh == tex.data:
            sys.exit('author_deltas: road texture %s: pixels not where the road has them' % filename)
        patches.append((addr, fresh))
    if len(patches) != 25:
        sys.exit('author_deltas: %d road textures, want 25' % len(patches))
    out = bytearray(base)
    for addr, fresh in sorted(patches):
        out[addr:addr + len(fresh)] = fresh
    return bytes(out)


def no_jdlz(name, data):
    """These targets are stored uncompressed; a JDLZ block would need huds_ops."""
    off = data.find(b'JDLZ')
    while off >= 0:
        if off % 64 == 0:
            sys.exit('author_deltas: %s holds a JDLZ block at %d' % (name, off))
        off = data.find(b'JDLZ', off + 1)


def cell(layers, s):
    """The target of a path for the subset s (bits X=1, H=2, R=4). With X:
    R over H over X (r3 rule). Without X: R0 over H0 (the mod over the
    untouched file); H files that do not depend on X are their own H0."""
    x, h, r, h0, r0 = layers
    if h is not None and r is not None:
        sys.exit('author_deltas: a path owned by both H and R')
    if s & 1:
        pick = (s & 4 and r) or (s & 2 and h) or x
    else:
        pick = (s & 4 and r0) or (s & 2 and h0)
    return pick or 'ORIG'


def main(out_dir, inc_path):
    os.makedirs(out_dir, exist_ok=True)
    report, deltas, files = [], [], {}

    # ---- the pack, checked file by file against its published listing
    rows = list(csv.reader(open(PACK_TSV), delimiter='\t'))[1:]
    pack = {}
    for name, size, want, _ in rows:
        p = os.path.join(PACK, name)
        if int(size) < (64 << 20):
            data = open(p, 'rb').read()
            if sha(data) != want:
                sys.exit('author_deltas: pack file %s is not the released one' % name)
            pack[name] = data
    by_sha = {}
    for name, data in pack.items():
        by_sha.setdefault(sha(data), name)
    pack_dds = [Src(d, 'X', n) for n, d in sorted(pack.items()) if n.lower().endswith('.dds')]

    # ---- the HUD 7z and the road ZIP, whole-file hashes, entries as released
    if file_sha(HUD_7Z) != HUD_SHA:
        sys.exit('author_deltas: the Upscaled HUD 7z is not the released one')
    hud = {}
    for base, dirs, names in os.walk(HUD_DIR):
        dirs.sort()
        for n in sorted(names):
            p = os.path.join(base, n)
            hud[os.path.relpath(p, HUD_DIR).replace(os.sep, '/')] = open(p, 'rb').read()
    road_raw = read(ROAD_ZIP, ROAD_SHA[:8])
    zf = zipfile.ZipFile(io.BytesIO(road_raw))
    road = {n: zf.read(n) for n in sorted(zf.namelist()) if not n.endswith('/')}

    def hud_dds(sub):
        return [Src(d, 'H', n) for n, d in sorted(hud.items()) if n.startswith('assets/' + sub + '/')
                and n.lower().endswith('.dds')]

    def one(name, target, sources, ops=None, note=''):
        data = [s.data for s in sources]
        if ops is None:
            ops = md.build_ops(target, data)
        delta, keep = md.finish(target, data, ops, os.path.join(out_dir, name), ())
        lit = md.literals(delta)
        for k in keep:
            s = sources[k]
            files[s.sha] = (s.sha, s.size, s.kind, s.name)
        deltas.append((name, lit))
        report.append(dict(file=name, delta_bytes=len(delta), literal_bytes=sum(len(x) for x in lit),
                           target_bytes=len(target), sources=len(keep), note=note,
                           target_sha=sha(target), delta_sha=sha(delta)))
        return sha(target), len(target)

    van_ingame = game('GLOBAL/InGameA.bun', 'f50d1422')
    van_race = game('GLOBAL/HUDTEXRACE.BIN', '7abb2c32')
    van_huds = game('GLOBAL/HUDS_Custom_00.bin', '02b8ce1d')
    van_attr = game('GLOBAL/attributes.bin', '3d0362f2')
    van_globalb_bun = game('GLOBAL/GLOBALB.BUN', 'a5c9a3d5')
    van_road_sha = file_sha(os.path.join(PC, 'TRACKS/STREAML2RA.BUN'))
    van_road_size = os.path.getsize(os.path.join(PC, 'TRACKS/STREAML2RA.BUN'))
    if not van_road_sha.startswith('0fb369d5'):
        sys.exit('author_deltas: the untouched road is not 0fb369d5')

    # ---- level X (the pack)
    x_attr = one('X_attributes.bin.jkd', read(os.path.join(EV, 'codex-p60-binary/smoke-asset-ba/original-attributes.bin'), 'c52182d2'),
                 [van_attr], note='original + the pack\'s game-setting values (VLT script, rebuilt)')
    x_globalb = one('X_GlobalB.lzc.jkd', read(os.path.join(EV, 'visual-mix/original/GlobalB.lzc'), 'a2eda728'),
                    [van_globalb_bun] + pack_dds, note='original GLOBALB.BUN + pack DDS (sky merge)')
    x_ingame_data = read(os.path.join(EV, 'codex-p60-binary/vis-final-wet-low/original-InGameA.bun'), '4eae8130')
    x_ingame = one('X_InGameA.bun.jkd', x_ingame_data, [van_ingame] + pack_dds, note='original + pack DDS (sky merge)')
    pack_ini = Src(pack['ModData/Files/scripts/X360Stuff.ini'], 'X', 'ModData/Files/scripts/X360Stuff.ini')
    x_ini = one('X_X360Stuff.ini.jkd', read(os.path.join(EV, 'codex-p60-binary/city-reflection-ab/original-X360Stuff.ini'), '8436599b'),
                [pack_ini], note='pack ini + JK4SPEED settings')
    # ---- level H (Upscaled HUD, over X)
    h_ingame = one('H_InGameA.bun.jkd', read(os.path.join(EV, 'codex-upscaled-hud/candidate/InGameA.bun'), '9825274a'),
                   [van_ingame] + pack_dds + hud_dds('InGameA'), note='original + pack DDS + Upscaled HUD DDS')
    h_race = one('H_HUDTEXRACE.BIN.jkd', read(os.path.join(EV, 'codex-upscaled-hud/candidate/HUDTEXRACE.BIN'), '6e57f398'),
                 [van_race] + hud_dds('HUDTEXRACE'), note='original + Upscaled HUD DDS')
    # ---- H without X: the HUD over the untouched InGameA. The HUD's script binds
    # InGameA/HUDTEXTURESGLOBAL/360v4+ only when X360Stuff.asi is there, so
    # without the pack the plain variants stay. The builder must first give
    # back the r3 H target over X (the same step with the 360v4+ group).
    def hud_group(folder):
        return [(n.rsplit('/', 1)[1][:-4], d) for n, d in sorted(hud.items())
                if n.startswith('assets/' + folder + '/') and n.lower().endswith('.dds')
                and '/' not in n[len('assets/' + folder + '/'):]]
    ingame_groups = [('INGAMETEXTURES', hud_group('InGameA/INGAMETEXTURES')),
                     ('HUDTEXTURESGLOBAL', hud_group('InGameA/HUDTEXTURESGLOBAL'))]
    plus = ('HUDTEXTURESGLOBAL', hud_group('InGameA/HUDTEXTURESGLOBAL/360v4+'))
    if sha(hud_build(x_ingame_data, ingame_groups + [plus])) != h_ingame[0]:
        sys.exit('author_deltas: the HUD builder does not give back the r3 X+H InGameA')
    h0_t = hud_build(van_ingame.data, ingame_groups)
    no_jdlz('H0 InGameA', h0_t)
    h0_ingame = one('H0_InGameA.bun.jkd', h0_t, [van_ingame] + hud_dds('InGameA'),
                    note='original + Upscaled HUD DDS (plain HUDTEXTURESGLOBAL, no pack)')
    del h0_t
    gauge = hud_dds('HUDS_Custom_00')
    huds_t = read(os.path.join(EV, 'jdlz-crash/HUDS_Custom_00.fixed.bin'), 'a67a45eb')
    h_huds = one('H_HUDS_Custom_00.bin.jkd', huds_t, [van_huds] + gauge,
                 ops=huds_ops(huds_t, van_huds.data, [g.data for g in gauge]),
                 note='original + Upscaled HUD gauge DDS, JDLZ made on the console (crash-fixed)')
    # ---- level R (HD road, over the X road)
    x_road_data = read(os.path.join(EV, 'visual-mix/original/STREAML2RA.BUN'), '17929677')
    x_road = Src(x_road_data, 'BUILT', 'TRACKS/STREAML2RA.BUN')
    road_dds = [Src(d, 'R', n) for n, d in road.items() if n.lower().endswith('.dds')]
    r_road = one('R_STREAML2RA.BUN.jkd', read(os.path.join(EV, 'road-visual/STREAML2RA.BUN'), 'e2a7a504'),
                 [x_road] + road_dds, note='the X road + N4SRazer DDS')
    if sha(road_build(x_road_data, road)) != r_road[0]:          # the builder gives back the r3 R target
        sys.exit('author_deltas: the road builder does not give back the r3 X+R road')
    del x_road_data, x_road
    # ---- R without X: the same 25 textures over the untouched road
    van_road = Src(read(os.path.join(PC, 'TRACKS/STREAML2RA.BUN'), '0fb369d5'), 'GAME', 'TRACKS/STREAML2RA.BUN')
    r0_t = road_build(van_road.data, road)
    if len(r0_t) != len(van_road.data):                           # pixels only, in the TPK data
        sys.exit('author_deltas: the R0 road changed size')
    r0_road = one('R0_STREAML2RA.BUN.jkd', r0_t, [van_road] + road_dds, note='the untouched road + N4SRazer DDS')
    del r0_t, van_road

    # ---- nothing from the pack, the HUD or the road in any literal
    B = md.B
    windows = {}
    for name, lit in deltas:
        for chunk in lit:
            for i in range(0, len(chunk) - B + 1):
                windows.setdefault(chunk[i:i + B], name)
    # a block of one or two byte values (zero fill, padding) is nobody's work
    windows = {k: v for k, v in windows.items() if len(set(k)) > 2}
    guards = [('pack ' + n, d) for n, d in pack.items()] + [('HUD ' + n, d) for n, d in hud.items()] + \
             [('road ' + n, d) for n, d in road.items()] + [('road zip', road_raw)]
    for gname, g in guards:
        for off in range(0, len(g) - B + 1, B):
            hit = windows.get(g[off:off + B])
            if hit:
                sys.exit('author_deltas: %s carries a 64-byte block of %s (offset %d: %s); refused'
                         % (hit, gname, off, g[off:off + B].hex()))

    # ---- verbatim pack files and the package's Xenon Effects
    ups = pack['Install-Part-3.ups']
    asi = pack['ModData/Files/scripts/X360Stuff.asi']
    shader_shas = {
        'car': '46d45c0bdbc921ff1ba92d7b0362fe243495e3e00fbff5f9c2a8be2aa4e50d83',
        'carnormalmap': '67946a29cdef62e3ee1f129042b91c336502f4f1a32b8df7819c00976a51b1b6',
        'debugPoly': '266e11361d566e7da0180f44ee79fe33e8dd7da8359f6e95c88b83e7b6e8ad64',
        'glassreflectshader': 'cde37809ead3eebdcc065429dad4f608d0cd12fe8154d49507dd70edb62f1b1c',
        'grass': '3d328d3a8827e9cc4faffcac38847f2c5058ac2cc3838c697783bb73e15a8fa8',
        'hdr': '3bd86ef4c60a0fec44d48c1cbdc8c8be396736b83b75362e13dd0aadeb44373f',
        'particles': 'a65890cf2b26f82c2841f69b247b9fa0cd7752503add20483f95bc59d1ca7bd4',
        'shadowscreenfilter': 'f4400ced679f56a07101b8aefbd5177873696d4721509d1ae9a095a5084cc0f3',
        'shadowvolume': '2468d6746b777cde99b267b5fbbede8518f487bcdfc24a41e53efe7502f8e057',
        'sky': 'ce9fbc32e9403cbff6fa52d81a761877fbb3e7610845d6530b5e6fbc91789a0f',
        'standard': 'a287f0ab377359487dc244d0d01c7de0114a693ce664fb842d512927475bdcd8',
        'trees': '80acf84aebba827e6f24283e61d439c4f506df77e65a966b13f10e1eee639b56',
        'visualtreatment': 'dcbf8fa0d6f8ad5e3f2df602fc5ba812346bec1d4b4c66ec4d5d0eddfb182bb2',
        'waterseashader': '1de277d831aebcccac6df8d68460f8a83246898a5ef97053feffedf50ed59391',
        'world': 'e59cf7cb15679bfaed0a640938fa26c4d9c3abc81c537611b9499b78fe4c13c2',
        'worldbone': '95749ef27c423ca134566d7dbe2e8b401b666b13c5298f814fa4a392dfc6258d',
        'worldnormalmap': '9adb16d7158889c59226e76a30e11942ec01d37ed852010048995444b0b5e72b',
        'worldreflect': 'a8f595409569e7a13a9c91e3be92ce4532f644a9a4696c80799c73eb7ed32ca0',
    }
    files[sha(ups)] = (sha(ups), len(ups), 'X', 'Install-Part-3.ups')
    files[sha(asi)] = (sha(asi), len(asi), 'X', 'ModData/Files/scripts/X360Stuff.asi')
    for s in shader_shas.values():
        if s not in by_sha:
            sys.exit('author_deltas: shader %s is not in the pack' % s)
        files[s] = (s, len(pack[by_sha[s]]), 'X', by_sha[s])
    xenon = {'scripts/NFSMW_XenonEffects.asi': '5dcad612', 'scripts/NFSMW_XenonEffects.ini': '140cb48f',
             'scripts/XenonEffects.tpk': 'b7d3fe2e'}
    for path, want in xenon.items():
        d = read(os.path.join(XDIR, path), want)
        files[sha(d)] = (sha(d), len(d), 'PKG', 'xenon/' + os.path.basename(path))
        xenon[path] = (sha(d), len(d))
    files[van_road_sha] = (van_road_sha, van_road_size, 'GAME', 'TRACKS/STREAML2RA.BUN')

    # ---- recipes (how a target is made) and paths (the target at each level)
    rec = []
    for r in report:
        rec.append((r['target_sha'], r['target_bytes'], 'DELTA', r['file'], None))
    rec.append(('17929677ee21330d0141ca704d32befa0648111ca4d0f192627c1636bb431e31', van_road_size, 'UPS', sha(ups), van_road_sha))
    rec.append((sha(asi), len(asi), 'COPY', sha(asi), None))
    for s in shader_shas.values():
        rec.append((s, files[s][1], 'COPY', s, None))
    for s, n in xenon.values():
        rec.append((s, n, 'COPY', s, None))

    paths = [
        # path, (X, H over X, R over X, H0, R0 without X); HUDTEXRACE and
        # HUDS_Custom_00 do not depend on the pack: their H is also their H0
        ('GLOBAL/attributes.bin', (x_attr, None, None, None, None)),
        ('GLOBAL/GlobalB.lzc', (x_globalb, None, None, None, None)),
        ('GLOBAL/InGameA.bun', (x_ingame, h_ingame, None, h0_ingame, None)),
        ('GLOBAL/HUDTEXRACE.BIN', ('ORIG', h_race, None, h_race, None)),
        ('GLOBAL/HUDS_Custom_00.bin', ('ORIG', h_huds, None, h_huds, None)),
        ('scripts/X360Stuff.ini', (x_ini, None, None, None, None)),
        ('scripts/X360Stuff.asi', ((sha(asi), len(asi)), None, None, None, None)),
        ('TRACKS/STREAML2RA.BUN', (('17929677ee21330d0141ca704d32befa0648111ca4d0f192627c1636bb431e31', van_road_size),
                                   None, r_road, None, r0_road)),
    ]
    for n, s in shader_shas.items():
        paths.append(('FX/COMPILED/%s.fx.o' % n, ((s, files[s][1]), None, None, None, None)))
    for p, v in xenon.items():
        paths.append((p, (v, None, None, None, None)))

    # every managed path the untouched game has: its hash, so the launcher's
    # pristine store (System/backup/pristine) accepts exactly that file
    for p, _ in paths:
        f = os.path.join(PC, p)
        if os.path.exists(f):
            s = file_sha(f)
            files.setdefault(s, (s, os.path.getsize(f), 'GAME', p))

    def c(s):
        return 'NULL' if s is None else '"%s"' % s

    def tgt(t):
        if t is None:
            return 'NULL'
        if t == 'ORIG':
            return '"="'
        return '"%s"' % t[0]
    out = ['/* SPDX-License-Identifier: LGPL-2.1-or-later */',
           '/* Copyright 2026 XxXJoker13XxX and contributors */',
           '/* Generated by launcher/title/author_deltas.py: do not edit. */',
           '',
           '/* Whole-archive pins (the pack has none: its download is a folder zip). */',
           'static const JkMsArchive MS_ARCHIVES[] = {',
           '    { JK_MOD_H, "%s", %dLL },' % (HUD_SHA, os.path.getsize(HUD_7Z)),
           '    { JK_MOD_R, "%s", %dLL },' % (ROAD_SHA, len(road_raw)),
           '};', '',
           '/* Files by hash: the player\'s game (GAME), an archive entry (X/H/R, by its',
           ' * path inside the release; X by suffix), a package file (PKG). */',
           'static const JkMsFile MS_FILES[] = {']
    kinds = {'GAME': 'JK_MS_GAME', 'X': 'JK_MOD_X', 'H': 'JK_MOD_H', 'R': 'JK_MOD_R', 'PKG': 'JK_MS_PKG'}
    for s, n, k, name in sorted(files.values(), key=lambda v: (v[2], v[3])):
        if k == 'BUILT':
            continue
        out.append('    { "%s", %dLL, %s, "%s" },' % (s, n, kinds[k], name))
    out += ['};', '', '/* How each target is made. */', 'static const JkMsRecipe MS_RECIPES[] = {']
    for s, n, how, arg, inp in rec:
        out.append('    { "%s", %dLL, JK_MS_%s, "%s", %s },' % (s, n, how, arg, c(inp)))
    out += ['};', '',
            '/* The game paths JK4SPEED manages and the file for each subset of the',
            ' * mods, at[set] with bits X=1, H=2, R=4: none, X, H, X+H, R, X+R, H+R,',
            ' * X+H+R. "=": the player\'s original (or no file). */',
            'static const JkMsPath MS_PATHS[] = {']
    for p, layers in paths:
        vanilla = 1 if os.path.exists(os.path.join(PC, p)) else 0     # the game has its own file there
        out.append('    { "%s", %d, { %s } },' % (p, vanilla, ', '.join(tgt(cell(layers, s)) for s in range(SETS))))
    out += ['};', '',
            '/* Never installed, never kept as an original. */',
            'static const char *const MS_BAD[] = {',
            '    "%s",   /* HUDS_Custom_00 with the JDLZ crash */' % BAD_HUDS,
            '};', '']
    if inc_path:
        with open(inc_path, 'w', newline='\n') as f:
            f.write('\n'.join(out))
    with open(os.path.join(out_dir, 'DELTAS.json'), 'w') as f:
        json.dump(report, f, indent=1)
    for r in report:
        print('%-26s delta %7d  literal %6d  target %10d  sources %3d  %s' %
              (r['file'], r['delta_bytes'], r['literal_bytes'], r['target_bytes'], r['sources'], r['note']))


SETS = 8                      # every subset of the three mods, bits X=1, H=2, R=4


def set_name(s):
    return '+'.join(m for m, b in (('X', 1), ('H', 2), ('R', 4)) if s & b) or 'none'


def verify_table(inc_path):
    """One line per managed path and subset: the target the catalog names, and
    whether the launcher can make it ("=" or a sha with a recipe). 0 when every
    cell is there, 1 otherwise."""
    import re
    text = open(inc_path).read()
    recipes = set(re.findall(r'^\s*\{ "([0-9a-f]{64})", \d+LL, JK_MS_', text, re.M))
    rows = re.findall(r'^\s*\{ "([^"]+)", ([01]), \{ ([^}]*) \} \},', text, re.M)
    bad = 0
    for path, vanilla, cells in rows:
        cells = [c.strip() for c in cells.split(',')]
        for s in range(SETS):
            c = cells[s] if s < len(cells) else 'NULL'
            if c == '"="':
                ok = 'ok'
            elif s == 0:
                ok = 'MISSING (none must be "=")'
            elif c.strip('"') in recipes:
                ok = 'ok'
            elif c == 'NULL':
                ok = 'MISSING'
            else:
                ok = 'MISSING (no recipe)'
            bad += ok != 'ok'
            print('%-36s %-6s %-66s %s' % (path, set_name(s), c.strip('"'), ok))
    if not rows:
        print('verify-table: no MS_PATHS rows in %s' % inc_path)
        return 1
    print('verify-table: %d paths x %d subsets, %d missing' % (len(rows), SETS, bad))
    return 1 if bad else 0


sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', 'mods'))
import bun  # noqa: E402

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--verify-table':
        sys.exit(verify_table(sys.argv[2] if len(sys.argv) > 2 else
                              os.path.join(os.path.dirname(HERE), 'src', 'jk_modset_catalog.inc')))
    main(sys.argv[1] if len(sys.argv) > 1 else 'deltas', sys.argv[2] if len(sys.argv) > 2 else None)

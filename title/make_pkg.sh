#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Package the JK4SPEED title (WSL), after build_title.sh:
#
#   /app0/eboot.bin, sce_module/relocated_wine_app.prx   this title (build_title.sh)
#   /app0/sce_module/*, win/**                           the bench package 282 runtime, unchanged
#   /app0/sce_module/dxvknative.prx                      our native DXVK server enc17 (r4: in the
#                                                        package, never written to /data/prospero-win)
#   /app0/sce_sys/param.sfo, icon0.png, pic1.png         JK4SPEED (our own art), keystone from 282
#   /app0/jk4speed-files/                                our files + manifest.txt (make_manifest.py),
#                                                        the config bases, deltas, the prefix seed
#   /app0/licenses/, docs/                               licence texts, player pages
#
# Not in the package: any game file, the X360 Stuff Pack, the HD road/HUD
# (players bring them: docs/launcher/MODS.md), and the Blur parts of the bench
# payload (blur-files/, native287/, seed245/). Every input is checked against
# its pinned release hash (docs/release/engine-1.0-manifest.md) first.
#
#   launcher/title/make_pkg.sh
#
# Output: $JK_TITLE_OUT/package/IV0000-<TITLE_ID>_00-JK4SPEED00000001.pkg
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
repo="$(cd "$launcher/.." && pwd)"
B="${JK_BOOTSTRAP:-$HOME/orbis-win-host-bootstrap}"
OUT="${JK_TITLE_OUT:-$B/jk4speed-title-r4}"
UROOT="${JK_BASE_UROOT:-$B/mwbench-package002/base/uroot}"
MAIN_REPO="${JK_MAIN_REPO:-$HOME/jk4speed-work}"
KIT="${JK_KIT:-$HOME/nfsmw-bench/kit/fusionfix}"
# the bench prefix the seed is made from: copied once into $OUT/seed-src, the original is only read
SEED_PREFIX="${JK_SEED_PREFIX:-$HOME/nfsmw-bench/kit/library/prefixes/nfsmw}"
TITLE_ID="${TITLE_ID:-JKSP00001}"
CONTENT_ID="IV0000-${TITLE_ID}_00-JK4SPEED00000001"
TOOLS="$B/native298-package277/orbis/sdk/orbis-sdk-v1/sdk/bin/linux"
PKGTOOL="$TOOLS/PkgTool.Core"
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# PkgTool.Core (.NET) needs OpenSSL 1.1; the build host has 3 only. A host
# x86-64 build of 1.1 sits in the ports cache (JK_OPENSSL11_LIB overrides).
export LD_LIBRARY_PATH="${JK_OPENSSL11_LIB:-$HOME/.cache/orbis-ports/openssl11/lib}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

say() { printf '== %s\n' "$*"; }
die() { echo "make_pkg: $*" >&2; exit 1; }
[ -f "$OUT/reloc-build/eboot.bin" ] || die "run build_title.sh first"
[ -x "$PKGTOOL" ] || die "missing $PKGTOOL"

STAGE="$OUT/pkg-stage"
PHASE="${JK_PKG_PHASE:-all}"     # all | stage (stop after release.txt) | pack (reuse the stage, sign, pack)
if [ "$PHASE" != pack ]; then
rm -rf "$STAGE" "$OUT/package"
mkdir -p "$STAGE/jk4speed-files" "$STAGE/sce_module" "$STAGE/sce_sys" "$STAGE/licenses/LICENSES" "$STAGE/docs" "$OUT/package"

# stage <name> <source> <sha256> [lf]: copy into jk4speed-files, optionally with CR stripped, and check
stage() {
    local name=$1 src=$2 want=$3 mode=${4:-}
    [ -f "$src" ] || die "missing source for $name: $src"
    if [ "$mode" = lf ]; then tr -d '\r' < "$src" > "$STAGE/jk4speed-files/$name"; else cp "$src" "$STAGE/jk4speed-files/$name"; fi
    local got
    got=$(sha256sum "$STAGE/jk4speed-files/$name" | cut -d' ' -f1)
    [ "$got" = "$want" ] || die "$name is $got, the release has $want"
    printf '   %s %s\n' "${got:0:16}" "$name"
}

say "jk4speed-files (release lock hashes)"
stage d3d9.dll "$MAIN_REPO/evidence/codex-re60/world-lod-queue-decl.dll" 28c101ff39c877ae9e63905d655568c62fd5bfcdea24a6ce57c9a9ae896dc207
stage dinput8.dll "$KIT/dinput8.dll" 63b465dbc1bf9c5ab4e33c3ccad099d9edb702f2c51587ac8d4f196497396437
stage Xidi.32.dll "$KIT/Xidi.32.dll" 226624efa1d7cddf258fcfd8e5e23afcd81f627077738cb9664d906cdfcc66c6
stage Xidi.ini "$here/config/Xidi.ini" 3c03a044672bff5af4860c9332dc94e2428a8ab983c38ac0d1029f58bbc46d54 lf
stage dxvk.conf "$repo/bench/dxvk/dxvk.conf" b54c72f6207f53ebb7bc08248958f814a03128266f8085d15bebff4210f32739 lf
# r4: the launcher composes <game>/jk4speed-dev.ini and System/profiles/nfsmw.profile from these
# two (jk_config.c reads them from jk4speed-files/; they are never installed as such)
stage jk4speed.ini.base "$here/config/jk4speed.ini.base" 9c2657410f02becc0f86023cb6547b0df3a43e282a83dfb50567d3bea63b8494 lf   # release: SHADOWS_NV=0 (owner, 2026-11-24: cars no longer go black)
stage nfsmw.profile.base "$here/config/nfsmw.profile.base" 7b1096cc51d633f2b9b971be1cc3b59d930fa946bcd073cf19d3a0f43d3520b8 lf

say "native DXVK server (sce_module/dxvknative.prx)"
# enc17 (our code on zlib DXVK). The runtime's module loader tries /data/prospero-win/override
# first, then sce_module/: a console without an override gets this one; r4 writes no override.
SERVER_SRC="${JK_SERVER_PRX:-$HOME/nfsmw-bench/native/server-enc17/link/dxvknative.prx}"
[ -f "$SERVER_SRC" ] || die "missing the native server: $SERVER_SRC"
cp "$SERVER_SRC" "$STAGE/sce_module/dxvknative.prx"
got=$(sha256sum "$STAGE/sce_module/dxvknative.prx" | cut -d' ' -f1)
[ "$got" = 86e62a08a2a88ee67faa20313fdf544bbdce5133cd67d86a8a496ab1322fbcf7 ] || die "dxvknative.prx is $got, the release has 86e62a08"
printf '   %s %s\n' "${got:0:16}" "sce_module/dxvknative.prx"
[ ! -e "$UROOT/sce_module/dxvknative.prx" ] || die "the 282 payload has its own dxvknative.prx: decide which one ships"

say "deltas (our derived files, made on the console from the player's own files)"
# The deltas Task 3 built (make_deltas.sh) are used as they are; JK_REBUILD_DELTAS=1 rebuilds
# them from the local author copies first. Every one is checked against its pin below.
if [ "${JK_REBUILD_DELTAS:-0}" = 1 ] || [ ! -f "$OUT/deltas/DELTAS.txt" ]; then bash "$here/make_deltas.sh"; fi
mkdir -p "$STAGE/jk4speed-files/deltas"
# the mod-set deltas (launcher/src/jk_modset_catalog.inc names them): X = the
# pack, H = Upscaled HUD, R = HD road; built from the player's own files and
# archive entries, never a byte of the mods themselves
for want in X_attributes.bin:9f3f785042634c3a1b26efa69d2afa338ec21c1193bca46a8a889e7db1fd6a93 \
            X_GlobalB.lzc:7c1992a24f2888a5473549ddc7d7d452937f0b11adfe2688bd1ac9066cc7b99c \
            X_InGameA.bun:4d8af63d41cf6ff1a984908dd70a6ed28e14fde48669f79a79593051adeceb69 \
            X_X360Stuff.ini:3941854b84c15941653298b5760c0250a3aca2b03185decde79b8b785727485e \
            H_InGameA.bun:ba28cdbe869435a6b3b2fcfe6052d826e27a1af11352ebe37d5b3004a7ec0e33 \
            H_HUDTEXRACE.BIN:bcd7de089d41e168c13d3464e0a46b7c69796dc0e2c7a82f4a79483050eb8bfd \
            H_HUDS_Custom_00.bin:ecdc31b37b08bfa607e8428d1c1e49794a393d4bfcc801f44481deb9cc14c37b \
            H0_InGameA.bun:38531d42b0b38c0990417f809d3fd376cc00ddfd751f2d3a25b41d19cea2cb18 \
            R_STREAML2RA.BUN:41429fdb9874b18d5531b66aa702ba9915bd3b5bf03869d6ea5a3f27d5f5e747 \
            R0_STREAML2RA.BUN:fa60c959f7e2fc24c5f9933741c5f70c05e66b3996a7e288da47d849d738aa2d; do
    name=${want%%:*}.jkd sha=${want#*:}
    cp "$OUT/deltas/$name" "$STAGE/jk4speed-files/deltas/$name"
    got=$(sha256sum "$STAGE/jk4speed-files/deltas/$name" | cut -d' ' -f1)
    [ "$got" = "$sha" ] || die "delta $name is $got, the pinned one is $sha"
    printf '   %s %s\n' "${got:0:16}" "deltas/$name"
done
# Xenon Effects (xan1242, MIT): bundled, installed by the mod set at level X
mkdir -p "$STAGE/jk4speed-files/xenon" "$STAGE/jk4speed-files/about"
for want in NFSMW_XenonEffects.asi:5dcad612125be0f13426644b044a2a18874fc2d84726c033e97975e262256692 \
            NFSMW_XenonEffects.ini:140cb48f22406fb3acce35756d959824bb1b9561b8d0a71aebd2a0d6c646b1e0 \
            XenonEffects.tpk:b7d3fe2ebdc520a650b0972eb71cf7ba0de27a6789a32501f5f8aee311008328; do
    name=${want%%:*} sha=${want#*:}
    cp "${JK_XENON_DIR:-$HOME/nfsmw-mods/NFSMW-X360/scripts}/$name" "$STAGE/jk4speed-files/xenon/$name" 2>/dev/null \
        || cp "$HOME/nfsmw-mods/NFSMW-X360/scripts/$name" "$STAGE/jk4speed-files/xenon/$name"
    got=$(sha256sum "$STAGE/jk4speed-files/xenon/$name" | cut -d' ' -f1)
    [ "$got" = "$sha" ] || die "xenon/$name is $got, the pinned one is $sha"
    printf '   %s %s\n' "${got:0:16}" "xenon/$name"
done
# every delta the launcher's catalog names is in the package, and nothing else
want_list=$(grep -o '"[A-Za-z0-9_.]*\.jkd"' "$launcher/src/jk_modset_catalog.inc" | tr -d '"' | sort -u)
have_list=$(cd "$STAGE/jk4speed-files/deltas" && ls *.jkd | sort)
[ "$want_list" = "$have_list" ] || die "the catalog names other deltas than the package ships: $(diff <(echo "$want_list") <(echo "$have_list") | tr '\n' ' ')"
cp "$launcher/assets/CREDITS.txt" "$STAGE/jk4speed-files/about/CREDITS.txt"

say "Wine prefix seed (jk4speed-files/seed, made from a copy of the bench prefix)"
[ -d "$OUT/seed-src" ] || { [ -f "$SEED_PREFIX/user.reg" ] || die "missing the bench prefix $SEED_PREFIX"; cp -a "$SEED_PREFIX" "$OUT/seed-src"; }
rm -rf "$OUT/seed"
python3 -I "$launcher/tools/make_seed.py" "$OUT/seed-src" "$OUT/seed" --app-root "$UROOT" | tee "$OUT/seed.txt"
got=$(sha256sum "$OUT/seed/index.txt" | cut -d' ' -f1)
SEED_INDEX_SHA=${JK_SEED_INDEX_SHA:-b00788f1806729db97c0ae672fb52ae817a342d9cde6ceb3599ed2528054c61e}
[ "$got" = "$SEED_INDEX_SHA" ] || die "seed index is $got, the pinned one is $SEED_INDEX_SHA"
cp -a "$OUT/seed" "$STAGE/jk4speed-files/seed"
printf '   %s seed/index.txt (%s objects)\n' "${got:0:16}" "$(ls "$STAGE/jk4speed-files/seed/objects" | wc -l)"
# every app: entry resolves inside this package's own win/ tree
awk '$1 == "f" { print $NF }' "$OUT/seed/index.txt" | grep '^app:' | sed 's/^app://' | sort -u | while read -r rel; do
    [ -f "$UROOT/$rel" ] || die "seed names $rel, the package has no such runtime file"
done
printf 'GLOBAL\nTRACKS\nCARS\nFRONTEND\n' > "$OUT/require.txt"
# r4: no ini, profile or server lines (the launcher composes the first two; the server is in sce_module/)
python3 -I "$launcher/tools/make_manifest.py" "$STAGE/jk4speed-files" --require-list "$OUT/require.txt"

# 1.1: the original game's package folder. Exactly the files the Black Edition's PATCH installs
# (its manifest's "file" lines), no X360 parts, plus ThirteenAG's Widescreen Fix (official release
# "nfsmw", GPL-3.0): the Black Edition gets its widescreen from X360 Stuff, the original from this.
say "the original game's package (jk4speed-files-nfs)"
NFSD="$STAGE/jk4speed-files-nfs"
mkdir -p "$NFSD/scripts"
awk '$1 == "file" { print $2 }' "$STAGE/jk4speed-files/manifest.txt" | while read -r rel; do
    mkdir -p "$NFSD/$(dirname "$rel")"
    cp "$STAGE/jk4speed-files/$rel" "$NFSD/$rel"
done
WSF_DIR="${JK_WSF_DIR:-$HOME/nfsmw-bench/kit/widescreen/scripts}"
for want in NFSMostWanted.WidescreenFix.asi:f3a7f8c04db295154e10987303a7237a8dc70e2ba944d38a79f895ad1f570dd1 \
            NFSMostWanted.WidescreenFix.tpk:a66fd12ebc549c4dc8b78dbc791e622d0e9065a286d698ac81a0dd15510bf904; do
    name=${want%%:*} sha=${want#*:}
    cp "$WSF_DIR/$name" "$NFSD/scripts/$name"
    got=$(sha256sum "$NFSD/scripts/$name" | cut -d' ' -f1)
    [ "$got" = "$sha" ] || die "scripts/$name is $got, the pinned release file is $sha"
    printf '   %s %s\n' "${got:0:16}" "jk4speed-files-nfs/scripts/$name"
done
tr -d '\r' < "$here/config/NFSMostWanted.WidescreenFix.ini" > "$NFSD/scripts/NFSMostWanted.WidescreenFix.ini"
# its own config bases (read by the launcher, never installed): the Black Edition's, minus the two
# shadow knobs made for the X360 folder, so the original decides its own shadows as on a PC
grep -v -E '^DXVK_PS4_NFS_(SHADOW_DETAIL|SHADOWS_NV)=' "$STAGE/jk4speed-files/jk4speed.ini.base" > "$NFSD/jk4speed.ini.base"
cp "$STAGE/jk4speed-files/nfsmw.profile.base" "$NFSD/nfsmw.profile.base"
grep -q 'SHADOW' "$NFSD/jk4speed.ini.base" && die "the original's config base still names a shadow knob" || true
# Other fixes from a PC install would change the game beyond the widescreen picture: Fusion Fix
# refuses to run next to the classic fix (a hidden MessageBox), Extra Options changes the game's
# options. PATCH moves them aside, Restore puts them back.
printf 'scripts/NFSMostWanted.FusionFix.asi\nscripts/NFSMWExtraOptions.asi\n' > "$OUT/nfs-remove.txt"
python3 -I "$launcher/tools/make_manifest.py" "$NFSD" --require-list "$OUT/require.txt" --remove-list "$OUT/nfs-remove.txt"
grep -q '^file scripts/NFSMostWanted.WidescreenFix.asi ' "$NFSD/manifest.txt" || die "the original's manifest has no Widescreen Fix"
grep -q -i 'x360\|xenon' "$NFSD/manifest.txt" && die "the original's manifest names an X360 part" || true

say "sce_sys"
bash "$launcher/tests/run.sh" make_sce_sys >/dev/null
cp "$launcher/build/sce_sys/icon0.png" "$launcher/build/sce_sys/pic1.png" "$STAGE/sce_sys/"
cp "$UROOT/sce_sys/keystone" "$STAGE/sce_sys/keystone"
SFO="$STAGE/sce_sys/param.sfo"
"$PKGTOOL" sfo_new "$SFO" >/dev/null
set_sfo() { "$PKGTOOL" sfo_setentry --value "$3" --type "$2" ${4:+--maxsize "$4"} "$SFO" "$1" >/dev/null; }
set_sfo APP_TYPE integer 1
set_sfo APP_VER utf8 01.10 8
set_sfo ATTRIBUTE integer 0x00010000
set_sfo CATEGORY utf8 gd 4
set_sfo CONTENT_ID utf8 "$CONTENT_ID" 48
set_sfo DOWNLOAD_DATA_SIZE integer 0
set_sfo PARENTAL_LEVEL integer 5
set_sfo PUBTOOLINFO utf8 "c_date=20261123,img0_l0_size=0,img0_l1_size=0,img0_sc_ksize=512,img0_pc_ksize=832" 512
set_sfo PUBTOOLVER integer 0x02890000
set_sfo SYSTEM_VER integer 0
set_sfo TITLE utf8 JK4SPEED 128
set_sfo TITLE_ID utf8 "$TITLE_ID" 12
set_sfo VERSION utf8 01.10 8
"$PKGTOOL" sfo_listentries "$SFO" | tee "$OUT/param-sfo.txt"

say "licences and player pages"
cp "$repo/release/package/LICENSE" "$repo/release/package/THIRD_PARTY.md" "$repo/release/package/SOURCES.txt" "$STAGE/licenses/"
cp "$repo"/release/package/LICENSES/* "$STAGE/licenses/LICENSES/"
cp "$launcher/assets/font/LICENCE.txt" "$STAGE/licenses/LICENSES/UbuntuFont-LICENCE.txt"
cp "$here"/licenses/*.txt "$STAGE/licenses/LICENSES/"      # Xenon Effects MIT, Xidi BSD-3, ASI Loader MIT, decoders
cp "$repo/docs/launcher/MODS.md" "$repo/docs/launcher/save-games.md" "$STAGE/docs/"
for page in README-player.md FAQ.md; do              # the player pages are part of every package
    [ -f "$repo/docs/launcher/$page" ] || die "docs/launcher/$page is missing: a package without it is not built"
    cp "$repo/docs/launcher/$page" "$STAGE/docs/"
done
# no player-facing page or config base names the bench runtime (constraint: no "prospero" in player text)
if grep -ril prospero "$STAGE/docs" "$STAGE/jk4speed-files/jk4speed.ini.base" "$STAGE/jk4speed-files/nfsmw.profile.base"; then
    die "a player page or config base says prospero"
fi
grep -qi prospero "$STAGE/jk4speed-files/about/CREDITS.txt" && echo "   WARN about/CREDITS.txt names the prospero runtime (credit line)" || true

say "production check (no test hook in the title)"
if [ "${JK_DEV:-0}" != 1 ] && grep -a -q -e "devtools on dir" -e "jk_devhook" "$OUT/reloc-build/prx/relocated_wine_app.prx" "$OUT/reloc-build/eboot.bin"; then
    die "the title carries the console test hook: build_title.sh without JK_DEV=1 for a release"
fi
echo "   no test hook"

fi   # end of the stage part (JK_PKG_PHASE=pack starts here)
[ -d "$STAGE/jk4speed-files" ] || die "no stage: run with JK_PKG_PHASE=stage first"

say "release list (Official Build badge, launcher/src/jk_release.h)"
python3 -I "$launcher/tools/make_release.py" "$STAGE" "JK4SPEED 1.1" --eboot "$OUT/reloc-build/eboot.bin"
REL_KEEP="${JK_RELEASE_DIR:-$HOME/jk4-build/release-sign}/jk4speed-files"
if [ -n "${JK_RELEASE_SIG:-}" ]; then
    # the owner signed an earlier, identical release.txt (tools/sign_release.sh)
    cmp -s "$REL_KEEP/release.txt" "$STAGE/jk4speed-files/release.txt" || die "release.txt changed since it was signed: sign again"
    cp "$JK_RELEASE_SIG" "$STAGE/jk4speed-files/release.sig"
    [ "$(stat -c %s "$STAGE/jk4speed-files/release.sig")" = 64 ] || die "release.sig is not 64 bytes"
    echo "   signed (Official Build)"
else
    mkdir -p "$REL_KEEP"
    cp "$STAGE/jk4speed-files/release.txt" "$REL_KEEP/release.txt"
    echo "   WARN unsigned: this package shows Unofficial build. To sign: bash launcher/tools/sign_release.sh ${REL_KEEP%/jk4speed-files}"
    echo "        then run make_pkg.sh again with JK_RELEASE_SIG=$REL_KEEP/release.sig"
fi

if [ "$PHASE" = stage ]; then
    echo "   stage done: sign $REL_KEEP/release.txt, then JK_PKG_PHASE=pack"
    exit 0
fi

say "file list"
LIST="$OUT/files.tsv"
: > "$LIST"
printf 'eboot.bin\t%s\n' "$OUT/reloc-build/eboot.bin" >> "$LIST"
printf 'sce_module/relocated_wine_app.prx\t%s\n' "$OUT/reloc-build/prx/relocated_wine_app.prx" >> "$LIST"
for f in "$UROOT"/sce_module/*; do
    n=$(basename "$f")
    [ "$n" = relocated_wine_app.prx ] && continue
    printf 'sce_module/%s\t%s\n' "$n" "$f" >> "$LIST"
done
(cd "$UROOT" && find win -type f | sort) | while read -r rel; do printf '%s\t%s\n' "$rel" "$UROOT/$rel"; done >> "$LIST"
(cd "$STAGE" && find . -type f | sed 's#^\./##' | sort) | while read -r rel; do printf '%s\t%s\n' "$rel" "$STAGE/$rel"; done >> "$LIST"
grep -qi -E '\.(bun|bin|lzc|fx\.o|asi|tpk)\b|speed\.exe' <(cut -f1 "$LIST" | grep -v -E '^win/|^sce_module/|^eboot\.bin$|^jk4speed-files/deltas/[A-Za-z0-9_.]+\.jkd$|^jk4speed-files/xenon/(NFSMW_XenonEffects\.(asi|ini)|XenonEffects\.tpk)$|^jk4speed-files-nfs/scripts/NFSMostWanted\.WidescreenFix\.(asi|tpk)$') && \
    die "a game or mod file is in the list" || true

say "gp4 + pkg"
python3 -I "$here/make_gp4.py" "$OUT/jk4speed.gp4" "$CONTENT_ID" "$LIST"
"$PKGTOOL" pkg_build "$OUT/jk4speed.gp4" "$OUT/package" > "$OUT/pkg-build.log" 2>&1 || { tail -20 "$OUT/pkg-build.log"; die "pkg_build failed"; }
PKG="$OUT/package/$CONTENT_ID.pkg"
[ -f "$PKG" ] || die "no package written"
"$PKGTOOL" pkg_validate "$PKG" > "$OUT/pkg-validate.log" 2>&1 || { tail -20 "$OUT/pkg-validate.log"; die "pkg_validate failed"; }
grep -c '\[OK\]' "$OUT/pkg-validate.log" | sed 's/^/   validate OK lines: /'
if grep -q '\[FAIL\]\|\[ERROR\]' "$OUT/pkg-validate.log"; then die "pkg_validate reported a failure"; fi

say "package"
sha256sum "$PKG" | tee "$OUT/package.sha256"
stat -c '%s bytes' "$PKG"

#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Build the JK4SPEED title (eboot.bin + sce_module/relocated_wine_app.prx) in
# WSL from the bench title tree native298-package277 (read-only input), the
# same way JkFlow builds BLUR00001 (JkFlow-export/title/README.md):
#
#   1. bake the launcher font and run the launcher host tests;
#   2. prepare $JK_TITLE_OUT: copy orbis/ and prospero/ of the base tree
#      (symbolic links kept), add launcher/src + the baked font +
#      jk_title_host.inc, apply title-jk4speed.patch, check the patched file;
#      only the folders this script makes are replaced (deltas/, seed/,
#      package/ and anything else in $JK_TITLE_OUT stay);
#   3. run the base tree's build-relocatable-wine-ps4.sh with JK4SPEED's
#      title id, the Blur seed off.
#
#   launcher/title/build_title.sh            (JK_SKIP_TESTS=1 skips step 1)
#
# Nothing outside $JK_TITLE_OUT is written; nothing is sent to a console.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/.." && pwd)"
B="${JK_BOOTSTRAP:-$HOME/orbis-win-host-bootstrap}"
BASE="$B/native298-package277"
OUT="${JK_TITLE_OUT:-$B/jk4speed-title-r4}"
UROOT="${JK_BASE_UROOT:-$B/mwbench-package002/base/uroot}"      # extracted bench package 282 payload
TITLE_ID="${TITLE_ID:-JKSP00001}"
BASE_MAIN_SHA=d4f56484184ee7c8a2f3f22ed71ef6f0f20ba01e0aa416ac6eb028e131cf43ed

say() { printf '== %s\n' "$*"; }
need() { [ -e "$1" ] || { echo "build_title: missing $1" >&2; exit 1; }; }
need "$BASE/orbis/scripts/build-relocatable-wine-ps4.sh"
need "$BASE/prospero/native/wine64_main.c"
need "$BASE/prospero/native/pw_diagnostics.c"
need "$UROOT/sce_module/libc.prx"
need "$UROOT/sce_module/libSceFios2.prx"
got=$(sha256sum "$BASE/prospero/native/wine64_main.c" | cut -d' ' -f1)
[ "$got" = "$BASE_MAIN_SHA" ] || { echo "build_title: base wine64_main.c is $got, want $BASE_MAIN_SHA" >&2; exit 1; }

if [ "${JK_SKIP_TESTS:-0}" != 1 ]; then
    say "launcher host tests"
    bash "$launcher/tests/run.sh"
else
    bash "$launcher/tools/bake_assets.sh" "$launcher/build/gen"
fi
need "$launcher/build/gen/jk_font_data.c"

say "prepare $OUT"
mkdir -p "$OUT"
for d in orbis prospero app-obj reloc-build tmp pkgout pkgout-base; do rm -rf "${OUT:?}/$d"; done
mkdir -p "$OUT/app-obj" "$OUT/reloc-build" "$OUT/tmp" "$OUT/prospero"
cp -a "$BASE/orbis" "$OUT/orbis"
for e in "$BASE"/prospero/* "$BASE"/prospero/.deps; do
    cp -a "$e" "$OUT/prospero/"
done
# src and native are real folders in the base tree: they are copies now, the rest stays linked
[ ! -L "$OUT/prospero/src" ] && [ ! -L "$OUT/prospero/native" ] || { echo "build_title: src/native are links" >&2; exit 1; }
cp "$launcher"/src/*.c "$launcher"/src/*.h "$launcher"/src/*.inc "$OUT/prospero/src/"
mkdir -p "$OUT/prospero/third_party/tweetnacl"   # r5: Ed25519 verify (public domain), included by src/jk_ed25519.c
cp "$launcher"/third_party/tweetnacl/tweetnacl.c "$launcher"/third_party/tweetnacl/tweetnacl.h "$OUT/prospero/third_party/tweetnacl/"
if [ "${JK_DEV:-0}" = 1 ]; then                 # bench title only: the console test hook
    cp "$launcher"/devtools/*.c "$launcher"/devtools/*.h "$OUT/prospero/src/"
fi
cp "$launcher/build/gen/jk_font.h" "$launcher/build/gen/jk_font_data.c" "$OUT/prospero/src/"
cp "$here/jk_title_host.inc" "$OUT/prospero/native/"
[ "${JK_DEV:-0}" = 1 ] && sed -i '1i #define JK_TITLE_PROD 0' "$OUT/prospero/native/jk_title_host.inc"   # bench title
(cd "$OUT" && patch -p1 --no-backup-if-mismatch < "$here/title-jk4speed.patch")
sha256sum "$OUT/prospero/native/wine64_main.c" "$OUT/prospero/native/pw_diagnostics.c" "$OUT/orbis/src/native/pw_wow_log_ps4.c" \
    "$OUT/prospero/native/jk_title_host.inc" > "$OUT/patched.sha256"
# r4: the Wine library root is /data/Jk4Speed/System
grep -q '#define JK_TITLE_DATA_ROOT "/data/Jk4Speed/System"' "$OUT/prospero/native/wine64_main.c" || { echo "build_title: patch did not set the r4 library root" >&2; exit 1; }

say "build (TITLE_ID=$TITLE_ID)"
cat > "$OUT/build-title.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
base=$B
out=$OUT
export PATH="\$base/native248-build-wine/tools:\$base/private-bin:\$base/deps/rootfs/usr/bin:\$PATH"
export MESA_BUILD=\$base/native248-platform
export TITLE_ID=$TITLE_ID TITLE=JK4SPEED PW_BUILD_ID=JK4SPEED-1.0-$TITLE_ID PKG_VERSION=01.00
export PW_ROOT="\$out/prospero" PW_APP_OBJ="\$out/app-obj" PW_BUILD_ROOT="\$out/reloc-build"
export PW_MODULE_STUBS_DIR="$UROOT/sce_module"
export PW_EBOOT_NO_RELRO=1 PW_SPLIT_RUNTIME=0 PW_WINE64_WAIT_WATCHDOG=0 PW_WINE64_SERVER_DIRECT_OFF=0 PW_WINE64_HEAP_FLEXIBLE=0
export PW_PS4_ROOT="\$base/orbis-win-9749fcd" TMPDIR="\$out/tmp"
export PKG_OUT="\$out/pkgout" PKG_BASE_OUT="\$out/pkgout-base"
unset N245_SEED_DIR N245_SEED_END PW_SHARED_LIBC PW_MESA_ZINK_DIR PS4_NTDLL32_DLL PS4_KERNELBASE32_DLL PS4_IPHLPAPI32_DLL
bash "\$out/orbis/scripts/build-relocatable-wine-ps4.sh"
EOF
bash "$OUT/build-title.sh" 2>&1 | tee "$OUT/build.log"

need "$OUT/reloc-build/eboot.bin"
need "$OUT/reloc-build/prx/relocated_wine_app.prx"
# r4: the title (eboot + its PRX) names /data/prospero-win only for the bench reads it keeps (dev.conf,
# the mprotect marker, r3's save folder copied once); anything else there means a write path came back.
bad=$(strings -n 8 "$OUT/reloc-build/prx/relocated_wine_app.prx" "$OUT/reloc-build/eboot.bin" | grep -o '/data/prospero-win[^ ]*' \
      | sort -u | grep -v -x -e /data/prospero-win/dev.conf -e /data/prospero-win/mprotect-nocache \
          -e /data/prospero-win/prefixes/nfsmw/drive_c/users/prospero/Documents/NFS || true)
[ -z "$bad" ] || { echo "build_title: the title still names $bad" >&2; exit 1; }
say "outputs"
sha256sum "$OUT/reloc-build/eboot.bin" "$OUT/reloc-build/prx/relocated_wine_app.prx" | tee "$OUT/outputs.sha256"

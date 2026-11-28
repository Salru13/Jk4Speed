#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Fast JK4SPEED release from the repo (WSL). Two steps around the owner's signature:
#
#   bash launcher/title/release.sh stage     copy HEAD, build the title only when its sources
#                                            changed (tests skipped: run launcher/tests/run.sh
#                                            on the commit first), stage the package, write
#                                            release.txt and print the signing command
#   (owner)  bash launcher/tools/sign_release.sh ~/jk4-build/release-sign
#   bash launcher/title/release.sh pack      pack once with the signature, check the badge
#                                            (OFFICIAL), copy the PKG to evidence/release/
#
# Env: JK_REPO (default: this checkout), JK_REL_DIR (default ~/jk4-build/rel),
#      JK_REL_VERSION (default 1.11). Nothing is sent to a console or to GitHub.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
REPO="${JK_REPO:-$(cd "$here/../.." && pwd)}"
REL="${JK_REL_DIR:-$HOME/jk4-build/rel}"
VER="${JK_REL_VERSION:-1.11}"
B="${JK_BOOTSTRAP:-$HOME/orbis-win-host-bootstrap}"
OUT="${JK_TITLE_OUT:-$B/jk4speed-title-r4}"
SIGN_DIR="${JK_RELEASE_DIR:-$HOME/jk4-build/release-sign}"
say() { printf '== %s\n' "$*"; }
die() { echo "release: $*" >&2; exit 1; }
t0=$(date +%s)
phase="${1:-}"

case "$phase" in
stage)
    say "copy HEAD of $REPO"
    head=$(git -C "$REPO" rev-parse --short HEAD)
    [ -z "$(git -C "$REPO" status --porcelain -- launcher)" ] || die "launcher/ has uncommitted changes: commit them first"
    rm -rf "$REL.new" && mkdir -p "$REL.new"
    git -C "$REPO" archive --format=tar HEAD | tar -x -C "$REL.new"
    find "$REL.new" -name '*.sh' -exec chmod +x {} +
    # keep the launcher's object cache between runs
    [ -d "$REL/launcher/build" ] && mv "$REL/launcher/build" "$REL.new/launcher/build"
    rm -rf "$REL" && mv "$REL.new" "$REL"
    echo "$head" > "$REL/.head"

    # the baked font/art the title build needs (cheap; tests themselves are not run)
    (cd "$REL" && bash launcher/tests/run.sh make_sce_sys > "$REL/bake.log" 2>&1) || { tail -5 "$REL/bake.log"; die "bake failed"; }

    # the title is rebuilt only when what goes into it changed
    stamp=$(git -C "$REPO" rev-parse HEAD:launcher/src HEAD:launcher/devtools HEAD:launcher/third_party \
            HEAD:launcher/title/jk_title_host.inc HEAD:launcher/title/build_title.sh \
            HEAD:launcher/title/title-jk4speed.patch | sha256sum | cut -c1-16)
    if [ -f "$OUT/reloc-build/eboot.bin" ] && [ "$(cat "$OUT/.title-stamp" 2>/dev/null)" = "$stamp" ]; then
        say "title unchanged ($stamp): reused"
    else
        say "title sources changed: build_title.sh (tests skipped)"
        (cd "$REL" && JK_SKIP_TESTS=1 bash launcher/title/build_title.sh > "$REL/build-title.log" 2>&1) \
            || { tail -20 "$REL/build-title.log"; die "build_title failed (log $REL/build-title.log)"; }
        echo "$stamp" > "$OUT/.title-stamp"
    fi

    say "stage the package"
    (cd "$REL" && JK_MAIN_REPO="$REPO" JK_PKG_PHASE=stage bash launcher/title/make_pkg.sh > "$REL/stage.log" 2>&1) \
        || { tail -20 "$REL/stage.log"; die "stage failed (log $REL/stage.log)"; }
    grep -E "no test hook|release.txt:" "$REL/stage.log" | sed 's/^/   /'
    echo
    echo "Now the owner signs (passphrase):"
    echo "   bash $REPO/launcher/tools/sign_release.sh $SIGN_DIR"
    echo "then: bash launcher/title/release.sh pack"
    ;;
pack)
    [ -f "$REL/.head" ] || die "run 'release.sh stage' first"
    sig="$SIGN_DIR/jk4speed-files/release.sig"
    [ -f "$sig" ] && [ "$sig" -nt "$SIGN_DIR/jk4speed-files/release.txt" ] || die "no fresh signature: sign release.txt first"
    say "pack once with the signature"
    (cd "$REL" && JK_MAIN_REPO="$REPO" JK_PKG_PHASE=pack JK_RELEASE_SIG="$sig" bash launcher/title/make_pkg.sh > "$REL/pack.log" 2>&1) \
        || { tail -20 "$REL/pack.log"; die "pack failed (log $REL/pack.log)"; }
    grep -E "signed|validate OK" "$REL/pack.log" | sed 's/^/   /'

    say "badge check (as the launcher does it)"
    L="$REL/launcher"; T=$(mktemp -d)
    cp -a "$OUT/pkg-stage" "$T/app"
    # the launcher PRX goes into the package from reloc-build (files.tsv), not from the stage: put it where
    # the console has it, so this check covers every listed file as the launcher sees them
    cp "$OUT/reloc-build/prx/relocated_wine_app.prx" "$T/app/sce_module/relocated_wine_app.prx"
    cat > "$T/v.c" <<'EOF'
#include "jk_release.h"
#include "jk_release_key.h"
#include <stdio.h>
int main(int c, char **v) { (void)c; JkRelease r; jk_release_verify(v[1], JK_RELEASE_PK, &r);
    printf("   badge: %s (%d files differ)\n", r.state == JK_REL_OFFICIAL ? "OFFICIAL" : "NOT OFFICIAL", r.changed);
    return r.state != JK_REL_OFFICIAL; }
EOF
    cc -std=c11 -D_DEFAULT_SOURCE -I"$L/src" -I"$L/build/gen" "$T/v.c" "$L/src/jk_release.c" "$L/src/jk_ed25519.c" \
        "$L/src/jk_fs.c" "$L/src/jk_sha256.c" -o "$T/v" 2>/dev/null || die "badge checker did not build"
    "$T/v" "$T/app" || { rm -rf "$T"; die "the package would not show Official Build"; }
    rm -rf "$T"

    pkg="$OUT/package/IV0000-JKSP00001_00-JK4SPEED00000001.pkg"
    h=$(sha256sum "$pkg" | cut -d' ' -f1)
    dest="$REPO/evidence/release"
    mkdir -p "$dest"
    cp "$pkg" "$dest/JK4SPEED-$VER-${h:0:8}.pkg"
    echo "$h  JK4SPEED-$VER-${h:0:8}.pkg" > "$dest/JK4SPEED-$VER-${h:0:8}.sha256"
    say "done: $dest/JK4SPEED-$VER-${h:0:8}.pkg"
    echo "   sha256 $h  (commit $(cat "$REL/.head"))"
    ;;
*)
    sed -n 2,16p "$0"; exit 2 ;;
esac
echo "   time: $(( $(date +%s) - t0 )) s"

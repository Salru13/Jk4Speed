#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Build the generated font and run the launcher host tests (WSL/Linux).
#   launcher/tests/run.sh               all tests
#   launcher/tests/run.sh test_ui       one test
# test_*.c run with ASan/UBSan; test_*.sh and test_*.py run after them.
# Objects are cached in build/obj and rebuilt when a source or header is newer.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
gen="$root/build/gen"
bin="$root/build/test"
mkdir -p "$gen" "$bin"

"$root/tools/bake_assets.sh" "$gen"

COMMON=(-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$root/src" -I"$root/devtools" -I"$gen" -I"$here"
        -DJK_TEST_DIR="\"$here\"" -DJK_ROOT_DIR="\"$root\"")
SAN=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
# devtools/: the title's console test hook (off unless <data_root>/devtools/enable exists)
SRC=("$root"/src/*.c "$root"/devtools/*.c "$gen/jk_font_data.c")
newest_header=$(ls -t "$root"/src/*.h "$root"/devtools/*.h "$gen"/*.h | head -1)

dir="$root/build/obj"
mkdir -p "$dir"
OBJ=()
for s in "${SRC[@]}"; do
    [ -f "$s" ] || continue
    o="$dir/$(basename "$s" .c).o"
    if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ "$newest_header" -nt "$o" ]; then
        cc "${COMMON[@]}" "${SAN[@]}" -c -o "$o" "$s"
    fi
    OBJ+=("$o")
done

tests=("$@")
if [ ${#tests[@]} -eq 0 ]; then
    for t in "$here"/test_*.c "$here"/test_*.sh "$here"/test_*.py; do
        [ -f "$t" ] && tests+=("$(basename "$t")")
    done
fi

status=0
for t in "${tests[@]}"; do
    case "$t" in
        *.sh) bash "$here/$t" || { echo "FAIL $t"; status=1; } ;;
        *.py) python3 -I "$here/$t" || { echo "FAIL $t"; status=1; } ;;
        *) t="${t%.c}"
           if cc "${COMMON[@]}" "${SAN[@]}" -o "$bin/$t" "$here/$t.c" "${OBJ[@]}" -lm; then
               "$bin/$t" || status=1
           else
               status=1
           fi ;;
    esac
done
if [ $status = 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $status

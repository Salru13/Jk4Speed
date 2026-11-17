#!/bin/bash
# Host-Wine acceptance test for mwprobe.exe. Run inside WSL after build.sh.
# Checks: PE flags match speed.exe's relevant ones, imports are only
# kernel32/msvcrt, the probe runs under host Wine, writes every log section,
# the deliberate no-access control case is caught as FAULT (proves the
# exception path), and the exit code is 0 (all heap cases executed).
set -u
OUT=${OUT:-$(cd "$(dirname "$0")/.." && pwd)/build/probe}
OBJDUMP=${OBJDUMP:-i686-w64-mingw32-objdump}
WINE=${WINE:-wine}
EXE=$OUT/mwprobe.exe
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }

check "exe exists" "[ -f '$EXE' ]"
[ -f "$EXE" ] || { echo "RESULT: FAIL"; exit 1; }

P=$("$OBJDUMP" -p "$EXE")
check "PE32 i386"            "echo \"\$P\" | grep -q 'file format pei-i386'"
check "ImageBase 0x400000"   "echo \"\$P\" | grep -Eq '^ImageBase[[:space:]]+0*400000'"
DLLCH=$((16#$(echo "$P" | awk '/^DllCharacteristics/{print $2}')))
CHARS=$((16#$(echo "$P" | awk '/^Characteristics/{sub(/^0x/,"",$2); print $2}')))
printf 'Characteristics=0x%04x DllCharacteristics=0x%04x\n' $CHARS $DLLCH
check "no NX_COMPAT (0x0100)"      "[ \$((DLLCH & 0x0100)) -eq 0 ]"
check "no DYNAMIC_BASE (0x0040)"   "[ \$((DLLCH & 0x0040)) -eq 0 ]"
check "not large-address-aware (0x0020)" "[ \$((CHARS & 0x0020)) -eq 0 ]"
check "relocs stripped (0x0001)"   "[ \$((CHARS & 0x0001)) -ne 0 ]"
IMPORTS=$(echo "$P" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u | tr '\n' ' ')
echo "imports: $IMPORTS"
check "imports only kernel32/msvcrt" "[ -z \"\$(echo $IMPORTS | tr ' ' '\n' | grep -v -e '^kernel32.dll$' -e '^msvcrt.dll$')\" ]"

export WINEPREFIX=$OUT/hostprefix WINEDEBUG=${WINEDEBUG:--all}
[ -d "$WINEPREFIX" ] || "$WINE" wineboot -i >/dev/null 2>&1
RUN=$OUT/run-host; rm -rf "$RUN"; mkdir -p "$RUN"; cp "$EXE" "$RUN/"
( cd "$RUN" && timeout 120 "$WINE" ./mwprobe.exe >/dev/null 2>&1 ); rc=$?
LOG=$RUN/mwprobe.log
echo "exit code: $rc"
check "log written" "[ -s '$LOG' ]"
for k in heap_process heap_created heap_created_64k malloc static_data va_rw va_rw_then_rx va_rwx; do
  check "case $k OK" "grep -Eq '^exec $k .* result=OK' '$LOG'"
done
check "control_noaccess caught as FAULT" "grep -Eq '^exec control_noaccess .* result=FAULT' '$LOG'"
check "addr walk summary" "grep -Eq '^vmsum regions=[0-9]+ free_total=0x[0-9a-f]+ largest_free=0x[0-9a-f]+' '$LOG'"
check "qpc freq"          "grep -Eq '^timer qpc_freq=[0-9]+' '$LOG'"
check "timeGetTime res"   "grep -Eq '^timer timegettime_res_ms=[0-9]+' '$LOG'"
for s in SleepEx1 Sleep1 Sleep0; do
  check "sleep $s stats" "grep -Eq '^sleep $s n=200 min_us=[0-9.]+ avg_us=[0-9.]+ max_us=[0-9.]+' '$LOG'"
done
check "done line"        "grep -Eq '^done exit=0' '$LOG'"
check "exit code 0"      "[ $rc -eq 0 ]"
[ $fail -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $fail

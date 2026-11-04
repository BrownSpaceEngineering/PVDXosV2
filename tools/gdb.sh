#!/usr/bin/env bash
# Batch GDB wrapper for PVDXos: connect, halt, run the given commands, resume, detach.
# Symbol setup mirrors `make connect`: stage-2 bootloader copy 1 as main file, plus the
# flash-linked app (src/PVDXos.elf) and stage-2 copies 2 and 3 via add-symbol-file.
# Usage: tools/gdb.sh "cmd1" "cmd2" ...
# Env:   GDB (default: gdb-multiarch | arm-none-eabi-gdb | gdb), GDB_HOST, GDB_PORT (2331),
#        GDB_TIMEOUT (s, default 30), GDB_MAXLINES (default 200),
#        GDB_RESUME (1 = resume target afterwards, default 1),
#        STAGE1_ELF STAGE2_1_ELF STAGE2_2_ELF STAGE2_3_ELF APP_ELF (override ELF paths)
set -u
cd "$(dirname "$0")/.." || exit 1
HOST=${GDB_HOST:-localhost}
PORT=${GDB_PORT:-2331}
T=${GDB_TIMEOUT:-30}
MAX=${GDB_MAXLINES:-200}
RESUME=${GDB_RESUME:-1}
STAGE1=${STAGE1_ELF:-bootloader/src/stage1.elf}
STAGE2_1=${STAGE2_1_ELF:-bootloader/src/stage2_1.elf}
STAGE2_2=${STAGE2_2_ELF:-bootloader/src/stage2_2.elf}
STAGE2_3=${STAGE2_3_ELF:-bootloader/src/stage2_3.elf}
APP=${APP_ELF:-src/PVDXos.elf}

GDB=${GDB:-}
if [ -z "$GDB" ]; then
  for g in gdb-multiarch arm-none-eabi-gdb gdb; do
    command -v "$g" >/dev/null 2>&1 && { GDB=$g; break; }
  done
fi
[ -n "$GDB" ] || { echo "no gdb found (install gdb-multiarch)" >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { echo "'timeout' not found (macOS: brew install coreutils; use gtimeout)" >&2; exit 2; }
[ $# -gt 0 ] || { echo "usage: $0 \"gdb command\" [\"gdb command\" ...]" >&2; exit 2; }
for f in "$STAGE2_1" "$APP"; do
  [ -f "$f" ] || { echo "missing $f (ELFs are gitignored; build first: make dev)" >&2; exit 2; }
done

sym=(-ex "set confirm off" -ex "add-symbol-file $APP")
[ -f "$STAGE1" ] && sym+=(-ex "add-symbol-file $STAGE1")
[ -f "$STAGE2_2" ] && sym+=(-ex "add-symbol-file $STAGE2_2")
[ -f "$STAGE2_3" ] && sym+=(-ex "add-symbol-file $STAGE2_3")

args=()
for c in "$@"; do args+=(-ex "$c"); done
tail_args=()
[ "$RESUME" = "1" ] && tail_args+=(-ex "monitor go")

out=$(timeout "$T" "$GDB" -batch -q "$STAGE2_1" \
  "${sym[@]}" \
  -x tools/gdb/helpers.gdb \
  -ex "target remote $HOST:$PORT" \
  -ex "monitor halt" \
  "${args[@]}" \
  ${tail_args[@]+"${tail_args[@]}"} \
  -ex "detach" 2>&1)
rc=$?

n=$(printf '%s\n' "$out" | wc -l)
printf '%s\n' "$out" | head -n "$MAX"
[ "$n" -gt "$MAX" ] && echo "[gdb.sh] output truncated ($n lines, showing $MAX; raise GDB_MAXLINES)" >&2
[ "$rc" -eq 124 ] && echo "[gdb.sh] TIMED OUT after ${T}s (server up? tools/jlink.sh status). If the board acts oddly, run: make reset_mcu" >&2
exit "$rc"

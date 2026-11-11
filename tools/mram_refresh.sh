#!/usr/bin/env bash
# Ground-only MRAM golden-copy refresh trigger (see bootloader/src/boot_record.h).
# Usage: tools/mram_refresh.sh request|cancel|status
#   request  write the refresh request word to backup RAM, verify it, then reset the board. Stage 2 copies the
#            flash OS image (+ bootloaders) to MRAM and clears the request only once the copy completes; an
#            interrupted or failed refresh is retried on the next boot. Watch progress with tools/rtt.sh.
#   cancel   clear the request word (no reset), e.g. to stop a refresh that keeps failing from retrying.
#   status   read the request word and the boot record (refresh_blocks counts up live during a refresh).
# request/cancel WRITE TARGET MEMORY (and request resets the board): agents need user approval to run them.
# Backup RAM survives resets but not a power cycle, so a pending request is lost if the board loses power.
# Env: JLINK_DEVICE (default ATSAMD51P20A), JLINK_SPEED (default 4000), JLINK_SERIAL
set -u
cd "$(dirname "$0")/.." || exit 1
DEV=${JLINK_DEVICE:-ATSAMD51P20A}
SPEED=${JLINK_SPEED:-4000}
HDR=bootloader/src/boot_record.h

define() { sed -n "s/^#define $1 (\(0x[0-9A-Fa-f]*\)u)/\1/p" "$HDR"; }
REQ_ADDR=$(define MRAM_REFRESH_REQUEST_ADDR)
REQ_MAGIC=$(define MRAM_REFRESH_REQUEST_MAGIC)
REC_ADDR=$(define BOOT_RECORD_ADDR)
REC_MAGIC=$(define BOOT_RECORD_MAGIC)
[ -n "$REQ_ADDR" ] && [ -n "$REQ_MAGIC" ] && [ -n "$REC_ADDR" ] && [ -n "$REC_MAGIC" ] ||
  { echo "could not read addresses from $HDR" >&2; exit 2; }
command -v JLinkExe >/dev/null || { echo "JLinkExe not in PATH" >&2; exit 2; }
if [ -f /tmp/pvdxos-jlink.pid ] && kill -0 "$(cat /tmp/pvdxos-jlink.pid)" 2>/dev/null; then
  echo "note: the J-Link GDB server (tools/jlink.sh) is running; if this fails to connect, stop it first" >&2
fi

# Runs J-Link Commander with the given commands after connecting; prints its output.
jlink() {
  local script
  script=$(mktemp)
  {
    echo "device $DEV"; echo "si SWD"; echo "speed $SPEED"; echo "connect"
    printf '%s\n' "$@"
    echo "exit"
  } > "$script"
  local sel=(); [ -n "${JLINK_SERIAL:-}" ] && sel=(-SelectEmuBySN "$JLINK_SERIAL")
  JLinkExe -NoGui 1 -ExitOnError 1 ${sel[@]+"${sel[@]}"} -CommanderScript "$script"
  local rc=$?
  rm -f "$script"
  return $rc
}

# mem32 prints "AAAAAAAA = W0 W1 W2 W3" (up to four words per line); load every word into MEM[address].
declare -A MEM
parse_mem() {
  local addr rest word a
  MEM=()
  while read -r addr rest; do
    a=$((16#$addr))
    for word in $rest; do MEM[$a]=$(printf '%s' "$word" | tr 'a-f' 'A-F'); a=$((a + 4)); done
  done < <(printf '%s\n' "$1" | tr -d '\r' | sed -n 's/^\([0-9A-Fa-f]\{8\}\) = \([0-9A-Fa-f ]*\).*/\1 \2/p')
}
# Prints the parsed word at address $1 (uppercase hex, no 0x), or nothing.
word_at() { printf '%s' "${MEM[$(( $1 ))]:-}"; }

case "${1:-}" in
request)
  out=$(jlink halt "w4 $REQ_ADDR $REQ_MAGIC" "mem32 $REQ_ADDR 1" go) || { printf '%s\n' "$out" >&2; exit 1; }
  parse_mem "$out"; value=$(word_at "$REQ_ADDR")
  if [ "$value" != "$(printf '%08X' "$REQ_MAGIC")" ]; then
    printf '%s\n' "$out" >&2
    echo "request word did not read back as $REQ_MAGIC (got '${value:-nothing}'); not resetting" >&2
    exit 1
  fi
  jlink r go > /dev/null || { echo "reset failed; the request is set and will run on the next reset" >&2; exit 1; }
  echo "MRAM refresh requested and board reset. Follow it with: tools/rtt.sh start; tools/rtt.sh tail"
  ;;
cancel)
  out=$(jlink "w4 $REQ_ADDR 0" "mem32 $REQ_ADDR 1") || { printf '%s\n' "$out" >&2; exit 1; }
  parse_mem "$out"; value=$(word_at "$REQ_ADDR")
  [ "$value" = "00000000" ] || { echo "request word reads '${value:-nothing}', not 0" >&2; exit 1; }
  echo "MRAM refresh request cleared"
  ;;
status)
  out=$(jlink "mem32 $REC_ADDR 16") || { printf '%s\n' "$out" >&2; exit 1; }
  parse_mem "$out"
  w() { word_at $(( REC_ADDR + $1 )); }
  req=$(word_at "$REQ_ADDR")
  [ -n "$req" ] || { printf '%s\n' "$out" >&2; echo "could not read backup RAM" >&2; exit 1; }
  if [ "$req" = "$(printf '%08X' "$REQ_MAGIC")" ]; then echo "refresh request: PENDING"; else echo "refresh request: none ($req)"; fi
  magic=$(w 0); w1=$((16#$(w 4))); w2=$((16#$(w 8))); w3=$((16#$(w 12))); w4=$((16#$(w 16)))
  if [ "$magic" = "$(printf '%08X' "$REC_MAGIC")" ]; then echo "boot record: valid (OS has not logged it yet)"
  else echo "boot record: magic $magic (the OS clears it after logging; fields below are from the last boot)"; fi
  echo "  stage-2 slot:          $(( w1 & 0xFF )) (255 = stage 1 fell back to the OS)"
  printf '  MRAM status:           init %02x, final %02x (2 bits per module, MRAM1 lowest: 1 ok, 2 degraded, 3 failed)\n' \
    $(( (w1 >> 8) & 0xFF )) $(( (w1 >> 16) & 0xFF ))
  echo "  refresh result:        $(( (w1 >> 24) & 0xFF )) (0 none, 1 ok, 2 no MRAM, 3 bad flash, 4 write failed)"
  echo "  refresh blocks copied: $(( (w4 >> 16) & 0xFFFF ))"
  echo "  refresh fail address:  0x$(w 20)"
  echo "  repair result:         $(( w2 & 0xFF )) (0 not run, 1 no MRAM, 2 header read failed, 3 no golden image, 4 done)"
  echo "  repair blocks:         $(( (w2 >> 16) & 0xFFFF )) checked, $(( w3 & 0xFFFF )) repaired," \
       "$(( (w3 >> 16) & 0xFFFF )) bad MRAM copy, $(( w4 & 0xFFFF )) program failures; header row rewritten: $(( (w2 >> 8) & 0xFF ))"
  ;;
*)
  echo "usage: $0 request|cancel|status"; exit 1 ;;
esac

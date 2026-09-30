#!/usr/bin/env bash
# J-Link GDB server manager (server only; RTT capture is tools/rtt.sh).
# Usage: tools/jlink.sh start|stop|status
# Env:   JLINK_DEVICE, JLINK_SPEED, JLINK_SERIAL, JLINK_RTOS (1=FreeRTOS plugin, default 1),
#        GDB_HOST (default localhost), GDB_PORT (default 2331)
set -u
DEV=${JLINK_DEVICE:-ATSAMD51P20A}
SPEED=${JLINK_SPEED:-4000}
HOST=${GDB_HOST:-localhost}
GDB_PORT=${GDB_PORT:-2331}
PIDF=/tmp/pvdxos-jlink.pid
LOG=/tmp/pvdxos-jlink.log

running()   { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }
port_open() { nc -z "$HOST" "$GDB_PORT" 2>/dev/null; }

case "${1:-}" in
start)
  if running; then echo "already running (pid $(cat "$PIDF"))"; exit 0; fi
  if port_open; then
    echo "a GDB server is already listening on $HOST:$GDB_PORT (started elsewhere); not starting another"
    exit 0
  fi
  command -v JLinkGDBServerCLExe >/dev/null || { echo "JLinkGDBServerCLExe not in PATH" >&2; exit 1; }
  command -v nc >/dev/null || { echo "nc (netcat) not in PATH" >&2; exit 1; }
  sel=(); [ -n "${JLINK_SERIAL:-}" ] && sel=(-select "USB=$JLINK_SERIAL")
  rtos=(); [ "${JLINK_RTOS:-1}" = "1" ] && rtos=(-rtos GDBServer/RTOSPlugin_FreeRTOS)
  # No -singlerun: the server must survive GDB disconnects (batch mode connects/detaches).
  nohup JLinkGDBServerCLExe -device "$DEV" -if SWD -speed "$SPEED" \
    -port "$GDB_PORT" -RTTTelnetPort 19021 -nogui -noir \
    ${rtos[@]+"${rtos[@]}"} ${sel[@]+"${sel[@]}"} > "$LOG" 2>&1 &
  echo $! > "$PIDF"
  for _ in $(seq 1 20); do
    running || break
    port_open && break
    sleep 0.5
  done
  if ! running || ! port_open; then
    echo "GDB server failed to come up. Log:" >&2
    tail -n 20 "$LOG" >&2
    running && kill "$(cat "$PIDF")" 2>/dev/null
    rm -f "$PIDF"
    exit 1
  fi
  echo "started (pid $(cat "$PIDF")); GDB on $HOST:$GDB_PORT; shell telnet on :19021"
  ;;
stop)
  if running; then
    kill "$(cat "$PIDF")" 2>/dev/null; rm -f "$PIDF"; echo "stopped"
  else
    rm -f "$PIDF"
    if port_open; then echo "nothing of ours to stop; a server started elsewhere is still listening on $HOST:$GDB_PORT"
    else echo "not running"; fi
  fi
  ;;
status)
  if running; then echo "RUNNING (ours) pid $(cat "$PIDF")"
  elif port_open; then echo "RUNNING (external) on $HOST:$GDB_PORT"
  else echo "NOT RUNNING"; fi
  [ -f "$LOG" ] && tail -n 15 "$LOG"
  ;;
*)
  echo "usage: $0 start|stop|status"; exit 1 ;;
esac

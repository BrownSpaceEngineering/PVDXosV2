#!/usr/bin/env bash
# Capture firmware logs (RTT up-channel 1) and framed JPEGs (channel 2) using
# scripts/rtt_logs.py. Images go to logs/images; runs in the background.
# Usage: tools/rtt.sh start|stop|status|tail [n]
set -u
cd "$(dirname "$0")/.." || exit 1
PIDF=/tmp/pvdxos-rtt.pid
OUT=/tmp/pvdxos-rtt.log

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

case "${1:-}" in
start)
  if running; then echo "already capturing (pid $(cat "$PIDF")) -> $OUT"; exit 0; fi
  python3 -c 'import pylink' 2>/dev/null || { echo "missing python module: pip install pylink-square" >&2; exit 1; }
  : > "$OUT"
  nohup python3 -u scripts/rtt_logs.py > "$OUT" 2>&1 &
  echo $! > "$PIDF"
  sleep 2
  if ! running; then
    echo "rtt_logs.py exited early. Output:" >&2
    tail -n 20 "$OUT" >&2
    rm -f "$PIDF"; exit 1
  fi
  echo "capturing RTT channel 1 -> $OUT; channel 2 JPEGs -> logs/images (also writes logs/<timestamp>.log)"
  ;;
stop)
  if running; then kill "$(cat "$PIDF")" 2>/dev/null; fi
  rm -f "$PIDF"; echo "stopped"
  ;;
status)
  if running; then echo "CAPTURING pid $(cat "$PIDF"), $(wc -c < "$OUT") bytes in $OUT"; else echo "NOT CAPTURING"; fi
  ;;
tail)
  tail -n "${2:-100}" "$OUT" 2>/dev/null || echo "no RTT log yet (run: tools/rtt.sh start)"
  ;;
*)
  echo "usage: $0 start|stop|status|tail [n]"; exit 1 ;;
esac

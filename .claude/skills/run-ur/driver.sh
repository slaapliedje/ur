#!/usr/bin/env bash
# Ur run-driver: launch a built ROM in atari800, send a key sequence, screenshot.
#
# Ur is a cross-compiled 6502 game (no native binary) — you "run" it by loading
# the build artifact in an emulator and driving it like a person would. This does
# that in ONE shot (launch -> drive -> capture -> kill) on purpose: a sandboxed
# desktop session tends to SIGSTKFLT (exit 144) any GUI process that outlives the
# command, so we never leave atari800 running in the background.
#
# Usage:
#   driver.sh <atari|a5200> <out.png> [key ...]
#     atari   -> build/atari/ur.xex   (A8, keyboard menu: keys 1-7)
#     a5200   -> build/a5200/ur.a52   (5200, controller keypad = host number keys)
#   keys: xdotool keysyms sent in order (e.g. 2 1 1 = vs-computer, roll, pick move 1).
#         With no keys it just boots and screenshots the title.
#
# Examples:
#   .claude/skills/run-ur/driver.sh atari /tmp/title.png
#   .claude/skills/run-ur/driver.sh atari /tmp/board.png 2 1
#   SDL_AUDIODRIVER= .claude/skills/run-ur/driver.sh atari /tmp/x.png   # with sound
set -u

TARGET="${1:?usage: driver.sh <atari|a5200> <out.png> [key ...]}"
OUT="${2:?output png path}"
shift 2
KEYS=("$@")

export DISPLAY="${DISPLAY:-:0}"
: "${SDL_AUDIODRIVER:=dummy}"; export SDL_AUDIODRIVER   # silent by default; set empty for audio

# 'sleep' is blocked (killed) in the sandbox; perl select() is a drop-in.
nap(){ perl -e "select(undef,undef,undef,${1})"; }

case "$TARGET" in
  atari|a8)   ARGS=(-windowed -nobasic -nojoystick build/atari/ur.xex);                                       BOOT=2.6 ;;
  a5200|5200) ARGS=(-5200 -5200-rev altirra -windowed -nojoystick -cart-type 4 -cart build/a5200/ur.a52);     BOOT=4.5 ;;
  *) echo "unknown target '$TARGET' (use: atari | a5200)"; exit 2 ;;
esac

LOG="/tmp/ur-driver-$$.log"
atari800 "${ARGS[@]}" >"$LOG" 2>&1 &
PID=$!
nap "$BOOT"                               # boot to the title (5200 is slower)

W=$(xdotool search --pid "$PID" 2>/dev/null | tail -1)
if [ -z "$W" ]; then
  echo "ERROR: no atari800 window (launch failed). Log:"; cat "$LOG"
  kill "$PID" 2>/dev/null; rm -f "$LOG"; exit 3
fi

xdotool windowactivate --sync "$W" 2>/dev/null   # focus so XTEST keys land (NOT --window)
for k in "${KEYS[@]}"; do
  xdotool keydown "$k"; nap 0.2; xdotool keyup "$k"; nap 0.7
done
nap 0.5
import -window "$W" "$OUT"

kill "$PID" 2>/dev/null
rm -f "$LOG"
[ -s "$OUT" ] && echo "wrote $OUT ($(identify -format '%wx%h' "$OUT" 2>/dev/null))" || { echo "ERROR: empty screenshot"; exit 4; }

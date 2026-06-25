#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Boot atari800 + FujiNet-PC into the FujiNet CONFIG menu (its lobby/host browser),
# or boot a specific client image passed as an argument (e.g. a dedicated FujiNet
# lobby app). Use this to test the public FujiNet lobby end to end: "Royal Game of
# Ur" (appkey 6) should appear and, when selected, download our client over TNFS
# (tnfs://thefnords.com/ur.xex) and launch it -> pick "3) Online".
#
# Usage:
#   tools/run-lobby.sh                  # cold-boot into the FujiNet CONFIG menu
#   tools/run-lobby.sh path/to/app.xex  # boot a specific client (.xex/.atr)
# Env: FUJINET_BIN, NPORT (9997), WPORT (8000), WIN_W, WIN_H
# Requires: atari800 (with -netsio) and a built FujiNet-PC. FujiNet-PC reaches the
#           internet via your host network, so it can hit lobby.fujinet.online and
#           the game/TNFS server at thefnords.com with no extra config.
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
CLIENT="${1:-${LOBBY_XEX:-}}"
FUJINET_BIN="${FUJINET_BIN:-$HOME/dev/fujinet-pc/build/fujinet}"
NPORT="${NPORT:-9997}"
WPORT="${WPORT:-8000}"
WIN_W="${WIN_W:-720}"
WIN_H="${WIN_H:-540}"
RUNDIR="$repo/build/lobby"                  # scratch (git-ignored under build/)

die() { echo "error: $*" >&2; exit 1; }

command -v atari800 >/dev/null 2>&1 || die "atari800 not found (needs -netsio, e.g. AUR atari800 6.1.0)"
[ -x "$FUJINET_BIN" ] || die "FujiNet-PC not found at '$FUJINET_BIN' (build it, or set FUJINET_BIN)"
[ -z "$CLIENT" ] || [ -f "$CLIENT" ] || die "client image not found: $CLIENT"

mkdir -p "$RUNDIR/SD"
# hsioindex=-1 disables high-speed SIO. With NetSIO, fast SIO makes the FujiNet
# CONFIG boot handshake loop ("Unexpected command frame at state 2"); standard
# speed boots reliably. It's FujiNet's shipped Atari default.
cat > "$RUNDIR/fnconfig.ini" <<CFG
[General]
hsioindex=-1
[NetSIO]
enabled=1
host=127.0.0.1
port=$NPORT
CFG

pids=()
cleanup() {
    echo; echo "stopping FujiNet-PC..."
    local p
    for p in "${pids[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null || true; done
    # the restart-loop subshell leaves a live fujinet child; reap it by path
    pkill -f "$FUJINET_BIN" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# FujiNet-PC exits 75 (EX_TEMPFAIL) on the Atari's cold boot; relaunch in a loop,
# like FujiNet's run-fujinet does (mirrors tools/online-test.sh).
( cd "$RUNDIR" || exit 1
  while :; do
      "$FUJINET_BIN" -c "$RUNDIR/fnconfig.ini" -s "$RUNDIR/SD" -u "http://0.0.0.0:$WPORT" >>fujinet.log 2>&1
      [ $? -eq 75 ] || break
  done ) &
pids+=($!)

echo "FujiNet web UI: http://localhost:$WPORT   (logs: $RUNDIR/fujinet.log)"
if [ -n "$CLIENT" ]; then
    echo "booting client image: $CLIENT"
    atari800 -netsio "$NPORT" -windowed -win-width "$WIN_W" -win-height "$WIN_H" -nobasic "$CLIENT"
else
    echo "cold-booting into the FujiNet CONFIG menu (no disk mounted)."
    echo "  -> open the lobby and look for 'Royal Game of Ur' (appkey 6); selecting it"
    echo "     downloads ur.xex over TNFS and boots it, then choose '3) Online'."
    atari800 -netsio "$NPORT" -windowed -win-width "$WIN_W" -win-height "$WIN_H" -nobasic
fi

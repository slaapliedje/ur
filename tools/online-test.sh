#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch a full local online test of the Royal Game of Ur:
#   the Go game server + N Atari/FujiNet-PC pairs (default 2 = a full match).
#
# Each pair is:  atari800 -netsio <PORT>   <->   FujiNet-PC (NetSIO host 127.0.0.1:<PORT>)
# with the emulated Atari booting build/atari/ur.xex. Pick "3) Online" in each
# Atari window; the server seats them Light/Dark and mediates the game.
#
# Usage:   tools/online-test.sh [num_pairs]      # default 2 (use 1 for a connect test)
# Env:     FUJINET_BIN, UR_XEX, UR_SERVER_BIN, WIN_W, WIN_H
# Requires: atari800 (with -netsio), a built FujiNet-PC, a built ur.xex, and Go
#           (to build the server) — it tells you how to get anything missing.
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
PAIRS="${1:-2}"
XEX="${UR_XEX:-$repo/build/atari/ur.xex}"
SERVER_BIN="${UR_SERVER_BIN:-$repo/server/ur-server}"
FUJINET_BIN="${FUJINET_BIN:-$HOME/dev/fujinet-pc/build/fujinet}"
RUNDIR="$repo/build/online"                 # scratch (git-ignored under build/)
WIN_W="${WIN_W:-720}"
WIN_H="${WIN_H:-540}"

die() { echo "error: $*" >&2; exit 1; }

command -v atari800 >/dev/null 2>&1 || die "atari800 not found (install it; needs -netsio support, e.g. AUR atari800 6.1.0)"
[ -x "$FUJINET_BIN" ] || die "FujiNet-PC not found at '$FUJINET_BIN' (build it, or set FUJINET_BIN)"
[ -f "$XEX" ] || die "no '$XEX' — build it first:  CSP_COMPAT=1 make atari   (cc65 <= 2.19)  or  make atari"

if [ ! -x "$SERVER_BIN" ]; then
    if command -v go >/dev/null 2>&1; then
        echo "building game server..."
        ( cd "$repo/server" && go build -o ur-server . ) || die "server build failed"
    else
        die "no server binary and Go is not installed. Install Go (pacman -S go), then: (cd server && go build -o ur-server .)"
    fi
fi

pids=()
cleanup() {
    echo; echo "stopping everything..."
    local p
    for p in "${pids[@]:-}"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null || true
    done
    # the restart-loop subshells leave a live fujinet child; reap them by path
    pkill -f "$FUJINET_BIN" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "starting game server ($SERVER_BIN)"
"$SERVER_BIN" &
pids+=($!)
sleep 1

start_pair() {                  # $1 = index, $2 = netsio/UDP port, $3 = web-UI port
    local i="$1" nport="$2" wport="$3" dir="$RUNDIR/p$i"
    mkdir -p "$dir/SD"
    cat > "$dir/fnconfig.ini" <<CFG
[NetSIO]
enabled=1
host=127.0.0.1
port=$nport
CFG
    echo "pair $i: atari800 netsio:$nport  fujinet web:$wport  ($dir)"
    atari800 -netsio "$nport" -windowed -win-width "$WIN_W" -win-height "$WIN_H" -nobasic "$XEX" &
    pids+=($!)
    # FujiNet-PC exits with code 75 (EX_TEMPFAIL) on an emulated reboot — which the
    # Atari does at cold boot. Relaunch it in a loop, like FujiNet's run-fujinet does.
    ( cd "$dir" || exit 1
      while :; do
          "$FUJINET_BIN" -c "$dir/fnconfig.ini" -s "$dir/SD" -u "http://0.0.0.0:$wport"
          [ $? -eq 75 ] || break
      done ) &
    pids+=($!)
}

base_net=9997
base_web=8000
for i in $(seq 1 "$PAIRS"); do
    start_pair "$i" "$((base_net + i - 1))" "$((base_web + i - 1))"
    sleep 1
done

echo
echo "Running: server + $PAIRS Atari/FujiNet pair(s)."
echo "In each Atari window choose '3) Online'. Ctrl-C here stops everything."
[ "$PAIRS" -lt 2 ] && echo "(With 1 pair the client will JOIN and wait for a second player.)"
wait

#!/usr/bin/env bash
# Online end-to-end test (no FujiNet needed): build the Go game server + the C
# net client (real proto.c/ur.c codec), start the server, connect TWO clients, and
# play a full server-authoritative game. Asserts both clients reach a clean game
# over with a consistent winner (7 pieces home).
#
# FujiNet is a transparent N:TCP pipe to this same server, so the bytes on the wire
# are identical — this validates the protocol + the server's mediation + that the
# shared C rules/codec agree with the Go server, which is the substance of the
# online path. (The FujiNet device itself is exercised separately via FujiNet-PC.)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PORT="${UR_TEST_PORT:-12345}"
OUT="build/online"
mkdir -p "$OUT"
DATA="$(mktemp -d)/stats.json"

echo "[online] building game server (go build)…"
( cd server && go build -o "$ROOT/$OUT/ur-server" . )

echo "[online] building net client (gcc + src/common)…"
cc -std=c99 -Wall -Wextra -Isrc/common -Isrc/net \
   -o "$OUT/ur-netclient" tools/ur-netclient.c src/common/proto.c src/common/ur.c

echo "[online] starting server on :$PORT (no HTTP, no AI fallback)…"
UR_ADDR=":$PORT" UR_HTTP_ADDR=off UR_AI_WAIT=off UR_DATA="$DATA" \
  "$OUT/ur-server" > "$OUT/server.log" 2>&1 &
SRV=$!
trap 'kill "$SRV" 2>/dev/null || true' EXIT

# wait until the server reports it's listening (do NOT open a probe connection —
# the server would accept it as a player and desync the pairing)
for _ in $(seq 1 50); do
  grep -q "listening on" "$OUT/server.log" 2>/dev/null && break
  sleep 0.1
done

echo "[online] connecting two clients and playing a full game…"
timeout 60 "$OUT/ur-netclient" 127.0.0.1 "$PORT" LIGHT & C0=$!
sleep 0.2   # let seat 0 connect first
timeout 60 "$OUT/ur-netclient" 127.0.0.1 "$PORT" DARK  & C1=$!

rc0=0; rc1=0
wait "$C0" || rc0=$?
wait "$C1" || rc1=$?

echo "[online] --- server log (tail) ---"
tail -n 6 "$OUT/server.log" | sed 's/^/[server] /'

if [ "$rc0" -eq 0 ] && [ "$rc1" -eq 0 ]; then
  echo "[online] PASS — both clients completed a server-authoritative game."
  exit 0
fi
echo "[online] FAIL — client exit codes: LIGHT=$rc0 DARK=$rc1"
exit 1

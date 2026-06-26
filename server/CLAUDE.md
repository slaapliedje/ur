# server — Ur game server (Go)

The server-authoritative host for a 2-player networked game. Retro clients
(Atari/Adam) reach it through FujiNet's `N:` device
(`N:TCP://<host>:<port>/`); the server owns the canonical game state, validates
moves, and broadcasts state to both clients.

> Parent context: [`/CLAUDE.md`](../CLAUDE.md). Wire protocol:
> [`docs/protocol.md`](../docs/protocol.md). The rules here mirror the portable C
> core ([`src/common/ur.c`](../src/common/ur.c)).

## Files

- `ur.go` — the rules (reimplemented in Go; mirrors `src/common/ur.c`).
- `proto.go` — the wire protocol; **must stay byte-identical** to
  `src/common/proto.{h,c}` and `docs/protocol.md`.
- `main.go` — the TCP server + game loop.
- `lobby.go` — FGS Lobby registration (opt-in).
- `*_test.go` — rules, encoding, and lobby-payload tests (run in CI).

## Build & run

```sh
cd server
go test ./...
go build -o ur-server .
UR_ADDR=":1234" ./ur-server      # default :1234
```

Point two clients at it via `N:TCP://<server-host>:1234/`. For local testing,
run it on the same machine as FujiNet-PC and use `N:TCP://localhost:1234/`
(matches `UR_NET_URL` in `src/atari/main.c`).

## Behaviour

Accepts two clients, assigns seats 0 (Light) and 1 (Dark), and runs the flow in
[`docs/protocol.md`](../docs/protocol.md): the on-turn client sends `ROLL` then
`MOVE`; the server applies the rules (capture / rosette extra-roll / bear-off /
win) and broadcasts a `STATE` snapshot to both after every change.

## FGS Lobby registration (`lobby.go`)

The server can advertise itself to the FujiNet lobby (`POST <lobby>/server` with a
`GameServer` JSON, refreshed every 30s and on player-count changes). It is
**opt-in** so local runs never touch the public lobby:

```sh
UR_LOBBY=1 \
UR_SERVER_URL="tcp://thefnords.com:1234/" \
UR_APPKEY=<assigned> \
UR_CLIENT_ATARI="tnfs://thefnords.com/ur.xex" \
UR_CLIENT_ADAM="tnfs://thefnords.com/ur.ddp" \
UR_CLIENT_C64="tnfs://thefnords.com/ur.prg" \
UR_CLIENT_APPLE2="tnfs://thefnords.com/ur.system" \
./ur-server
```

The **appkey is per-game, not per-platform** — one `UR_APPKEY` covers all four; the
platforms differ only by their `clients[]` download URL. Each platform is advertised
only if its `UR_CLIENT_<PLAT>` is set (an empty url makes the lobby reject the POST),
so you can roll platforms out one at a time. Platform ids are `atari`/`adam`/`c64`/
`apple2` (fujinet-lib names).

If you'd rather serve the binaries from this server instead of TNFS, the HTTP
listener (`web.go`) exposes `/ur.xex`, `/ur.ddp`, `/ur.prg`, `/ur.system` (override
the served file per platform with `UR_CLIENT_<PLAT>_FILE`); point the `UR_CLIENT_*`
URLs at `http://thefnords.com:8080/...`.

Env: `UR_LOBBY` (enable), `UR_LOBBY_URL` (default `https://lobby.fujinet.online/server`),
`UR_SERVER_NAME`, `UR_REGION`, `UR_SERVER_URL`, `UR_APPKEY`,
`UR_CLIENT_{ATARI,ADAM,C64,APPLE2}` (+ optional `…_FILE` to set the served path).

**Still needed for real discoverability** (external/manual): the client binaries
**hosted** (TNFS or this server's HTTP) at the advertised URLs, and a per-platform
**lobby client** app that lists games and launches ours (the standard FujiNet lobby
clients — see [`src/net/CLAUDE.md`](../src/net/CLAUDE.md)).

## Single-player vs the AI

If a second human doesn't connect within `UR_AI_WAIT` seconds (default 6;
`UR_AI_WAIT=off` to disable), the server seats its own AI as seat 1 (`runGameAI`)
so a lone player still gets a game — the FGS "AI room" pattern. The AI heuristic
is `aiPick` in `ur.go`. AI games are **not** recorded to the leaderboard.

## v1 limitations (future work)

- One game at a time (then it loops to accept the next player/pair).
- Minimal error handling; no reconnect/resume.
- Naive pairing (no liveness check); a stale socket can be paired in.

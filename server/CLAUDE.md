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
- `ur_test.go` — rules + encoding tests (run in CI).

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

## v1 limitations (future work)

- One game at a time (then it loops to accept the next pair).
- Minimal error handling; no reconnect/resume.
- No FGS Lobby registration yet (so games are joined by direct address, not via
  the `fujinet.online` lobby). Registering with the lobby is the next networking
  milestone — see [`src/net/CLAUDE.md`](../src/net/CLAUDE.md).

# Hosting the Ur server

The Go server in [`server/`](../server/) hosts online multiplayer **and** the
player leaderboard in one process. You don't need a separate web server — it
opens a TCP listener for the game and an HTTP listener for the leaderboard.

## What you need

- A host that can run a long-lived process (a small VPS, or a home machine with
  port-forwarding + a dynamic-DNS name).
- Two reachable ports (defaults): **TCP 1234** (clients connect here via FujiNet
  `N:TCP://host:1234/`) and **HTTP 8080** (leaderboard web page + the `/top` feed
  the in-game leaderboard fetches over `N:HTTP`). Open them in your firewall.

## Configuration (environment variables)

| Variable | Default | Purpose |
|----------|---------|---------|
| `UR_ADDR` | `:1234` | TCP listen address for the game |
| `UR_HTTP_ADDR` | `:8080` | HTTP listen address for the leaderboard (`off` disables) |
| `UR_DATA` | `ur-stats.json` | leaderboard JSON file (persisted, atomically written) |
| `UR_LOBBY` | unset | set to `1` to register with the public FGS lobby |
| `UR_LOBBY_URL` | `https://lobby.fujinet.online/server` | lobby endpoint |
| `UR_SERVER_NAME` | `ur-1` | server name shown in the lobby |
| `UR_SERVER_URL` | `tcp://localhost:1234/` | **public** URL clients should dial (set this for lobby use) |
| `UR_REGION` | `us` | 2-char region code |
| `UR_APPKEY` | `0` | FGS game id (1–255, assigned by the FujiNet project) |

## Run with Docker

```sh
docker build -t ur-server server/
docker run -d --name ur -p 1234:1234 -p 8080:8080 -v ur-data:/data ur-server
```

The volume keeps `ur-stats.json` across restarts. Visit `http://HOST:8080/` for
the leaderboard.

## Run with systemd

Build the binary (`cd server && go build -o ur-server .`), copy it to
`/usr/local/bin/`, then install [`server/ur-server.service`](../server/ur-server.service)
(it documents the steps in its header). Stats live in `/var/lib/ur/`.

## Leaderboard endpoints

The HTTP listener serves:

- `GET /` — human-friendly HTML leaderboard.
- `GET /leaderboard.json` — full table as JSON.
- `GET /top` — compact binary for 8-bit clients: one count byte, then up to ten
  records of `name[3]` + `wins` (uint16, little-endian). This is what the Atari
  client's **Leaderboard** screen fetches over `N:HTTP`.

Players are keyed by the 3-letter **name** they set on the title screen (stored
in their FujiNet AppKey) and sent to the server on `JOIN`. Names are
self-asserted — fine for a friendly board; add a per-name token later if you need
it cheat-resistant.

## Pointing clients at your host

The client's endpoints are compile-time constants in
[`src/atari/main.c`](../src/atari/main.c): `UR_NET_URL` (`N:TCP://localhost:1234/`)
and `UR_TOP_URL` (`N:HTTP://localhost:8080/top`). For a public build, change
`localhost` to your host and rebuild. (Making these runtime-configurable is a
future improvement.)

## Registering with the FGS lobby

Set `UR_LOBBY=1`, a public `UR_SERVER_URL`, and a real `UR_APPKEY` (1–255). The
server then POSTs its details to the lobby on start, on a 30s heartbeat, and when
the player count changes, so the FujiNet lobby clients can discover and launch
your game. See [`src/net/CLAUDE.md`](../src/net/CLAUDE.md) and
[`server/lobby.go`](../server/lobby.go).

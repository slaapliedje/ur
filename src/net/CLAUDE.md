# src/net — networking & FujiNet Game System glue

The 6502-side networking layer: it talks to FujiNet's `N:` device through
`fujinet-lib`, handles the FujiNet Game System (FGS) lobby/game-client flow, and
moves wire bytes between the game core and the game server.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). The wire-protocol spec lives in
> [`docs/protocol.md`](../../docs/protocol.md); the pure encode/decode codec lives
> in [`src/common`](../common/CLAUDE.md). This directory is the **transport**, not
> the serialization.

## The FujiNet `N:` device

FujiNet gives an 8-bit machine internet access via a network device addressed like
any other peripheral. Vintage CPUs can't run a TCP/IP stack, so FujiNet does it and
exposes a simple device interface.

- **Units:** `N1:`–`N4:` (up to four concurrent connections; `N:` defaults to unit 1).
- **Devicespec / URL:** `N:TCP://host:port/`, `N:UDP://host:port/`,
  `N:HTTP://host/path`, plus HTTPS, TNFS, etc.
- **Operations:** open, read, write, close, status — the same conceptual model on
  every platform.
- **Transport differs, API doesn't:** SIO bus on Atari, SmartPort on Apple II, IEC
  bus on C64, **AdamNet** (serial, RJ12) on the Coleco Adam. `fujinet-lib` hides
  these differences, so our client code is the same shape across platforms.

Use **[fujinet-lib](https://github.com/FujiNetWIFI/fujinet-lib)** for the `N:`
calls — it is multi-toolchain and ships all four of our targets (atari, apple2,
c64, and **adam**), built with cc65 for the 6502 machines and z88dk for the Z80
Adam — and **[fujinet-build-tools](https://github.com/FujiNetWIFI/fujinet-build-tools)**
as the project/build template. The `plat_net_*` functions in the platform interface
should be thin shims over fujinet-lib.

## FujiNet Game System (FGS)

FGS is the proven model for cross-platform, turn-based multiplayer on FujiNet
(5 Card Stud, Fujitzee). Notably, **Connect 4 already does live ADAM↔Atari
cross-play** — i.e. across the Z80/6502 CPU divide — which is exactly the
cross-platform-multiplayer goal of this project, demonstrated to work. Four pieces:

1. **Lobby** — a persistent service at `fujinet.online`. Game servers register with
   it; lobby clients query it for active games and open seats.
2. **Lobby client** — per-platform; lists available games and **launches** the
   matching game client.
3. **Game server** — **server-authoritative**; holds the true game state, validates
   moves, and tells clients what to display. Registers with the Lobby via JSON
   (game name, connection/mount info, player counts). See `server/` below.
4. **Game client** — our Ur client; connects to the game server (typically
   `N:TCP`), sends the local player's actions, and renders the state the server
   returns.

Reference implementations to model on:
- Servers + Lobby (Go): https://github.com/FujiNetWIFI/servers (`/lobby`, `/5cardstud`)
- Clients: https://github.com/FujiNetWIFI/fujinet-apps (`/lobby`, `/5cardstud`)

## Our Ur protocol

The wire protocol is **the cross-platform contract**: identical bytes on Atari,
Apple II, and C64. Design goals:

- **Server-authoritative.** The client never trusts its own copy for legality; the
  server owns the canonical state and adjudicates rolls, captures, and bear-off.
- **Compact and cheap to parse on a 6502.** Prefer a small, fixed-layout encoding
  (or minimal ASCII/JSON-light) — parsing JSON on an 8-bit CPU is costly, so keep
  messages small and regular.
- **Turn-based, low bandwidth.** Message families: seat/join, board-state snapshot,
  roll, move, capture notification, turn handoff, game-over, error/heartbeat.

The encode/decode functions live in [`src/common`](../common/CLAUDE.md) (pure,
testable); this directory calls them and handles the actual open/read/write/close.
The authoritative message catalog is [`docs/protocol.md`](../../docs/protocol.md).

## The game server (`server/`, future)

A modern, server-authoritative game server (the Go `5cardstud` server is the
reference; final language TBD). Responsibilities: hold canonical Ur state, validate
moves against the rules, mediate between two clients, and register with the FGS
Lobby. It can reuse the rules from `src/common` conceptually, but is a separate
modern-platform program. When that code starts, add a dedicated `server/CLAUDE.md`.

## Testing the network path

- Run **[FujiNet-PC](https://github.com/FujiNetWIFI/fujinet-pc)** to emulate FujiNet
  with no hardware.
- Run the game server locally; point clients at it via `N:TCP://<host>:<port>/`.
- Drive two emulator instances (e.g. two Altirra windows, or Altirra + VICE) as the
  two players to exercise cross-platform turns end-to-end.

# Ur multiplayer wire protocol (seed spec)

> **Status: draft / placeholder.** This is the seed for the cross-platform
> multiplayer contract. It will be filled in as the networking work begins. The
> guiding decisions below are firm; the concrete message byte layouts are not yet
> finalized.

This document defines the bytes exchanged between the Ur **game client** (running
on Atari / Apple II / C64) and the **game server**. It is the single source of
truth that makes cross-platform play work: every client, on every machine, speaks
exactly this protocol. See [`src/net/CLAUDE.md`](../src/net/CLAUDE.md) for how it
fits into the FujiNet Game System, and [`src/common/CLAUDE.md`](../src/common/CLAUDE.md)
for where the encode/decode codec lives.

## Design principles

1. **Server-authoritative.** The server owns canonical game state and adjudicates
   every roll, move, capture, and bear-off. Clients send intents; the server
   returns truth. Clients render what they're told.
2. **Cheap on a 6502.** Small, fixed-layout messages. Avoid heavy parsing; if any
   text encoding is used, keep it minimal and regular. Bandwidth and CPU are scarce.
3. **Platform-independent (and CPU-independent).** No platform- or CPU-specific
   assumptions — the same bytes must be produced and consumed by 6502 clients
   (Atari/Apple II/C64) and the Z80 Coleco Adam alike. Multi-byte fields, if any,
   use a fixed, documented byte order. (Both CPU families are little-endian, but do
   not rely on that — define the order explicitly.)
4. **Turn-based, stateless-ish polling.** Ur is turn-based and low-bandwidth; the
   client can poll the server for state changes between local actions.

## Transport

- Default: `N:TCP://<server-host>:<port>/` via FujiNet's `N:` device (`fujinet-lib`).
- The server is discovered/launched through the FGS Lobby at `fujinet.online`.

## Message families (to be specified)

| Family            | Direction        | Purpose                                          |
|-------------------|------------------|--------------------------------------------------|
| `JOIN` / `SEAT`   | client → server  | Join a game, claim a seat / get assigned a color |
| `STATE`           | server → client  | Full board snapshot (piece positions, scores)    |
| `ROLL`            | both             | Request a dice roll / report the rolled value    |
| `MOVE`            | client → server  | Move a piece; server validates and applies        |
| `CAPTURE`         | server → client  | Notify that a piece was sent back to start        |
| `TURN`            | server → client  | Whose turn it is / extra-roll grant (rosette)    |
| `GAMEOVER`        | server → client  | Winner declared                                  |
| `PING`/`ERROR`    | both             | Heartbeat / error reporting                       |

Exact opcodes, field widths, and byte order: **TBD** — define before implementing
the codec in `src/common`.

## Open questions

- Binary fixed-layout vs minimal ASCII framing (lean binary for 6502 parsing cost).
- Polling cadence and timeouts over the `N:` device.
- Reconnect / resume semantics if a client drops mid-game.
- How closely to mirror the existing 5 Card Stud server conventions for Lobby
  compatibility.

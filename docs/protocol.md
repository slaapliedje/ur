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

Encoded and decoded by [`src/common/proto.c`](../src/common/proto.c) (see
`proto.h`); covered by round-trip tests in `tests/test_ur.c`. Byte 0 of every
message is its type.

### Client → server

| Msg    | Byte | Bytes                          | Purpose                         |
|--------|------|--------------------------------|---------------------------------|
| `JOIN` | 0x01 | `[0]=0x01 [1]=version(1)`      | Join; server assigns a seat     |
| `ROLL` | 0x02 | `[0]=0x02`                     | Request a dice roll             |
| `MOVE` | 0x03 | `[0]=0x03 [1]=piece index 0..6`| Move one of your pieces         |

### Server → client

`STATE` (0x81) — a fixed **21-byte** snapshot the client renders:

| Off | Field    | Meaning                                                      |
|-----|----------|--------------------------------------------------------------|
| 0   | type     | `0x81`                                                       |
| 1   | seat     | which player THIS client controls (0/1)                      |
| 2   | turn     | whose turn it is (0/1)                                        |
| 3   | phase    | 0 = await roll, 1 = await move, 2 = game over                |
| 4   | roll     | 0–4, or `0xFF` if none                                       |
| 5   | winner   | 0/1, or `0xFF` if not over                                   |
| 6   | flags    | bit0 captured, bit1 rosette, bit2 scored (from the last move)|
| 7–13| light[7] | Light's 7 piece positions (0=start … 15=home)                |
|14–20| dark[7]  | Dark's 7 piece positions                                     |

Because both ends share the rules core, the client computes its own legal moves
from `turn`/`roll`/positions (`ur_legal_moves`); the server need not send them.

### Flow

1. Client connects, sends `JOIN`; server replies `STATE` (assigns `seat`).
2. On its turn (`seat == turn`, phase = await-roll) the client sends `ROLL`. The
   server rolls; if moves exist → phase = await-move (with `roll`), else it
   advances the turn. It broadcasts `STATE`.
3. Client (await-move) shows legal moves, sends `MOVE`. Server validates, applies
   (capture / rosette extra-roll / bear-off / win), advances turn, broadcasts `STATE`.
4. The off-turn client polls (`network_read`) for `STATE` and renders.

## Open questions

- Polling cadence and timeouts over the `N:` device.
- Reconnect / resume semantics if a client drops mid-game.
- How the server registers with the FGS Lobby (mount info, player counts).

# Architecture

Technical design behind [`/CLAUDE.md`](../CLAUDE.md). Covers the module layering,
the core data model, and per-platform memory maps. Game *feel* is in
[`design.md`](design.md); rules are in [`rules.md`](rules.md).

## Layering

```
            +-------------------------------------------------+
            |                 src/common (C)                  |
            |  rules · move-gen · AI · turn state machine ·   |
            |  protocol codec        (portable, deterministic)|
            +------------------------+------------------------+
                                     | calls only
                            src/common/plat.h   (the contract)
                                     | implemented by
   +-----------+-----------+---------+---------+---------------+
   | src/atari | src/c64   | src/apple2        | src/adam      |
   | (cc65)    | (cc65)    | (cc65)            | (z88dk/Z80)   |
   |  display · sound · input · timing · net-shim per machine  |
   +-----------------------------------------------------------+
                                     |
                          src/net  (fujinet-lib, N: device)
                                     |
                       game server (server/, modern) <-> FGS Lobby
```

**Rule:** the core calls the platform via `plat.h`; the platform never reaches
into core internals. The core has no `#ifdef PLATFORM` and no toolchain-specific
code, so the *same* C compiles under cc65 and z88dk. See
[`src/common/CLAUDE.md`](../src/common/CLAUDE.md) and
[`src/common/plat.h`](../src/common/plat.h).

## Core data model (sketch — refine in code)

- **Board / path:** fixed lookup tables mapping each player's 14 path steps to
  board squares, with rosette steps flagged. (Indices pending verification — see
  [`rules.md`](rules.md).)
- **Piece state:** for each of a player's 7 pieces, its path position
  (0 = not yet entered … 15 = borne off). A compact `uint8_t` per piece.
- **`struct game_state`** (opaque to platforms): the two players' piece arrays,
  whose turn, the current/last roll, pending extra-roll flag, and scores
  (pieces home per player). Kept small and fixed-size — no dynamic allocation.
- **Determinism:** identical inputs + RNG stream ⇒ identical state, so the server
  and all clients agree. The RNG is in the core; only its *seed* comes from the
  platform (`plat_rng_seed()`).

## Turn state machine (sketch)

```
AWAIT_ROLL --roll--> (roll==0 || no legal move) --> PASS --> (other player) AWAIT_ROLL
                 \--> AWAIT_MOVE --move--> [capture?] --> [landed on rosette?]
                                                            |yes-> AWAIT_ROLL (same player)
                                                            |no --> (other player) AWAIT_ROLL
[all 7 home] --> GAME_OVER
```

## Per-platform memory maps & RAM budget

8-bit machines have hard, fixed memory layouts; plan code/data placement early.
Values below are starting references — **finalize per platform during bring-up**.

| Platform | CPU / clock | RAM | Default code load | Graphics memory | Sound |
|----------|-------------|-----|-------------------|-----------------|-------|
| Atari 8-bit | 6502 @ 1.79MHz | 48–64K | cc65 `atari`: `$2000` | display list + screen RAM; **P/M graphics page-aligned** | POKEY |
| Coleco Adam | Z80 @ 3.58MHz | 64–80K (banked) | z88dk per target | **TMS9928A VRAM (separate, via ports)** | SN76489 |
| Commodore 64 | 6510 @ 1.02MHz | 64K (banked ROM/IO) | cc65 `c64`: `$0801` (BASIC stub) | VIC-II screen/charset/bitmap; **sprite pointers** | SID |
| Apple II | 6502 @ 1.02MHz | 48–64K | cc65 `apple2`: `$0803` | hi-res pages `$2000`/`$4000` | 1-bit speaker |

Key per-platform constraints to nail down in each `src/<plat>/CLAUDE.md`:

- **Atari:** display-list location, screen RAM, P/M graphics base (2K/1K aligned),
  charset base, zero-page budget shared with the OS.
- **Adam:** VRAM is *not* CPU-addressable — all video goes through VDP ports; bank
  switching for the 64K+ map; EOS vs CP/M memory model.
- **C64:** the VIC-II bank, sprite data pointers, charset vs bitmap mode, and the
  `$D000`–`$DFFF` IO/charset banking.
- **Apple II:** which hi-res page is active/displayed (page flipping), and the
  awkward 7-pixel-per-byte color layout.

> Zero page (6502) / the low registers (Z80) are scarce — reserve them for the
> hottest core and rendering variables, and document the allocation per platform.

## Networking & server

The 6502/Z80 clients are thin: they render server-authoritative state and send
intents. The wire protocol ([`protocol.md`](protocol.md)) is CPU-independent so an
Atari and an Adam can play each other. The server (`server/`) reimplements the
rules on a modern platform; share **test vectors** with `src/common` so both
engines provably agree. Details: [`src/net/CLAUDE.md`](../src/net/CLAUDE.md).

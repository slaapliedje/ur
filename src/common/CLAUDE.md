# src/common — portable game core

Platform-independent heart of the game. Everything here compiles **unchanged** on
every target — the 6502 machines (Atari, Apple II, C64) via `cc65`, **and** the
Z80-based Coleco Adam via `z88dk`/SDCC. This is where the actual rules of Ur live.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Rules detail:
> [`docs/rules.md`](../../docs/rules.md). Wire protocol: [`docs/protocol.md`](../../docs/protocol.md).

## Hard rules for this directory

- **No platform headers, no hardware access, no platform I/O.** No `#include`
  of Atari/Apple/C64 system headers, no PEEK/POKE, no direct register writes.
- **No `#ifdef PLATFORM`.** If something differs per machine, it belongs in a
  platform layer behind the platform interface (below), not here.
- **Toolchain-neutral.** This code is compiled by two different C toolchains
  (`cc65` for 6502, `z88dk`/SDCC for the Z80 Adam). Stick to portable standard C —
  no cc65-specific or z88dk-specific extensions, pragmas, or library calls. If it
  doesn't compile cleanly under both, it doesn't belong here.
- **Deterministic.** Given the same inputs (including the same RNG seed/stream),
  the core must always produce the same state. The game server and every client
  run this same logic and must agree.
- **Statically allocated, fixed-size data.** No dynamic allocation. RAM is tiny
  (~48–64K, often less). Prefer `uint8_t`; the 6502 is 8-bit, little-endian, and
  16-bit math is comparatively expensive.

## What lives here

- **Board model & path tables** — the 20-square board and each player's 14-square
  path (private entry → shared middle → private exit). Encoded as lookup tables.
  Derive these from [`docs/rules.md`](../../docs/rules.md) only after the exact
  indices are verified against authoritative sources.
- **Dice** — portable RNG plus the binary-tetrahedral roll (0–4, distribution
  1/4/6/4/1 of 16). The RNG *seed/entropy* comes from the platform layer
  (`plat_*`), but the roll logic itself is here and deterministic given the stream.
- **Move generation & validation** — legal moves for a given roll and board state.
- **Rules** — capture (shared middle row), rosette safety + extra roll, exact-roll
  bear-off, win detection (all 7 pieces home).
- **Turn state machine** — whose turn, awaiting-roll vs awaiting-move, extra-roll
  handling, game-over.
- **AI opponent** — single-player bot(s); pure functions over board state.
- **Protocol codec** — pure encode/decode between game state/moves and the wire
  bytes defined in [`docs/protocol.md`](../../docs/protocol.md). Only serialization
  lives here; the actual send/receive (FujiNet transport) is in
  [`src/net`](../net/CLAUDE.md).
- **Title music** (`music.{h,c}`) — the **Hurrian Hymn** as platform-neutral data:
  `ur_hymn[]` is a list of `{note, dur}` steps (`note` = absolute MIDI number or
  `MUSIC_REST`; `dur` in eighth-note ticks). Each platform owns a tiny `play_hymn()`
  that maps the MIDI number to its sound chip and sets the tempo — the core never
  touches hardware. `ur_hymn_len` is a literal (sccz80 rejects `sizeof` in a
  file-scope const initializer), so keep it in sync with the array.

## The platform interface (the boundary)

`common/` declares an abstract interface that each platform layer implements. The
core **calls** these; platform code never calls back up into private core state.
Expected families (names indicative — finalize when the first code lands):

- `plat_draw_*` — render board, pieces, dice, UI.
- `plat_input_*` — read joystick/keyboard, return abstract actions.
- `plat_sound_*` — play effects/music.
- `plat_net_*` — open/read/write/close a connection (thin shim over `fujinet-lib`).
- `plat_rng_seed()` / timing — entropy and frame/timing hooks.

Keep this interface small and stable; it is the contract the three platform layers
must each satisfy.

## Testing

Because the core is pure C with no hardware dependencies, it should be unit-testable
on a host machine (compile the `common/` sources with a normal host C compiler,
stub the `plat_*` interface). Aim to test rules, move generation, capture/bear-off
edge cases, and protocol round-tripping there before running on hardware/emulator.

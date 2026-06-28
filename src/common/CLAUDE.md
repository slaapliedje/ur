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
- **AI opponent** — `ur_ai_pick(s, player, roll, level)`, a pure function over board
  state, at three difficulties: `UR_AI_EASY` (random legal move), `UR_AI_NORMAL`
  (1-ply greedy on the positional eval), `UR_AI_HARD` (greedy + capture/rosette/
  bear-off bonuses and a shared-lane capture-risk penalty). The shared controller
  asks `plat_pick_level()` per vs-AI game; ports implement the chooser.
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

## The platform interface + shared controller (the boundary)

The local game flow is owned by a **shared controller**, `ur_game.c`, so ports no
longer copy the turn loop. It calls a small, real interface declared in
[`plat.h`](plat.h); each platform layer implements it:

- `plat_draw(roll, msg)` — render the board + HUD + a status line for `ur_g`.
- `plat_wait()` — block until one confirm press.
- `plat_choose_move(player, roll)` — render the move list and return the pick (−1 = none).
- `plat_animate(player, from, to)` — slide the moving token (empty stub if no animation).
- `plat_sfx_roll()` / `plat_sfx_result(res)` — sound.
- `plat_seed()` — RNG entropy (hardware source, or input-timing accumulator).

`ur_run_game(vs_ai)` runs one game over `ur_g` (the controller-owned `ur_state`) and
returns the winner; the port's `main()` does its title/menu, calls it, shows the
result, loops. The core **calls** these; platform code never calls back into private
core state.

**Adoption is incremental.** `ur_game.c` is opt-in per port via `$(UR_GAME_SRC)` in
the makefiles (it is *filtered out* of `COMMON_SOURCES`), so a port can keep its own
loop until converted. **All ports now use it** — Atari (+5200), C64, Apple II, Adam
(+ColecoVision), SMS (+Game Gear), Game Boy, NES. The four FujiNet ports keep a
separate `online_game` loop (NOT part of this contract — see
[`src/net`](../net/CLAUDE.md)); only their *local* play goes through the controller.
Note `plat_roll(roll)` does the roll sound + any dice animation (e.g. the Atari/C64
tumble); `plat_animate(player,from,to)` does any token glide (the SMS slide; the
Atari folds in its cursor-hide + destination highlight); both are no-ops on ports
without them.

Controller strings are **UPPERCASE / `A-Z 0-9 space !` only** — the lowest common
denominator across the ports' fonts (the NES font is uppercase-only), so they render
everywhere.

Keep this interface small and stable; it is the contract the platform layers satisfy.

## Testing

Because the core is pure C with no hardware dependencies, it should be unit-testable
on a host machine (compile the `common/` sources with a normal host C compiler,
stub the `plat_*` interface). Aim to test rules, move generation, capture/bear-off
edge cases, and protocol round-tripping there before running on hardware/emulator.

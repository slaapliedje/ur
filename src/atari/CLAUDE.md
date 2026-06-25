# src/atari — Atari 8-bit platform layer (primary target)

The Atari 8-bit (400/800/XL/XE) implementation of the platform interface. This is
the **first and primary** target — get the game fully playable here before porting.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` interface declared by [`src/common`](../common/CLAUDE.md). Keep all
> Atari-specific code here; never push hardware details into `common/`.

## Hardware

- **CPU:** MOS 6502 @ ~1.79 MHz (NTSC) / ~1.77 MHz (PAL).
- **RAM:** up to 64K (XL/XE); ~48K typically usable under DOS/BASIC. Zero page is
  shared with the OS — budget it carefully.
- **ANTIC + GTIA:** programmable display via **display lists**; many graphics and
  text modes; hardware **player/missile graphics** (sprites) — ideal for the pieces
  and cursor/highlights.
- **POKEY:** 4 sound channels, keyboard scan, serial I/O, and a hardware RNG at
  `RANDOM` (`$D20A`) — a good entropy source for `plat_rng_seed()`.
- **PIA:** joystick ports and control lines.

## Display / input / sound approach (TBD)

Not yet decided — document the choice here once made. Likely direction:

- **Board:** a custom display list, or a text mode with a **redefined character set**
  drawing the 20 squares, rosettes, and bridge.
- **Pieces & cursor:** **player/missile graphics** overlaid on the board.
- **Input:** joystick (port 1) for selecting/moving a piece; console keys
  (Start/Select/Option) for menus.
- **Sound:** POKEY for dice-roll, move, capture, and win effects.

## FujiNet

FujiNet attaches to the Atari **SIO bus**. Use `fujinet-lib` (atari target) for the
`N:` device; the Atari was FujiNet's original platform, so support is the most
mature here. See [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## Build & run

- **Compile/link:** `cc65 -t atari` (or `-t atarixl` for an XL/XE-targeted build),
  then `ld65` with the appropriate config. Output a `.xex` executable, or wrap into
  a `.atr` disk image (e.g. via `mkatr`/`dir2atr`) for emulators and FujiNet.
- **Hand-tuned asm:** use `ca65` for inline/critical routines; **MADS** is an option
  for Atari-only hot paths (display-list/VBI/DLI handlers) if `ca65` proves awkward.
- **Run:** load the `.xex`/`.atr` in **Altirra**.
- **Network test:** Altirra + **FujiNet-PC**, or real FujiNet hardware on a real Atari.

> `cc65`, MADS, and Altirra are developer prerequisites and are not assumed
> installed in this environment.

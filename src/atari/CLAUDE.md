# src/atari — Atari 8-bit platform layer (primary target)

The Atari 8-bit (400/800/XL/XE) implementation of the platform interface. This is
the **first and primary** target, and the **visual reference** the other ports aim
toward (see [`docs/visual-design.md`](../../docs/visual-design.md)). Local hot-seat,
vs-AI, and FujiNet online all work, with the full visual treatment in place.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` interface declared by [`src/common`](../common/CLAUDE.md). Keep all
> Atari-specific code here; never push hardware details into `common/`.

## Files

- [`main.c`](main.c) — game flow, the horizontal board renderer (`draw_all`), move
  selection, the dice-tumble animation, and the FujiNet online loop.
- [`atarihw.c`](atarihw.c)/[`.h`](atarihw.h) — the hardware layer: colour registers,
  the custom charset (carved/eye/dots tiles, gold-flower rosettes, charset disc
  tokens, dice, cursor glyphs), the mode-4 board display-list patch, the in-game
  **DLI** field sheen, POKEY sound effects, and joystick input. (The PMG token
  helpers remain but are unused since the board went horizontal — see below.)
- [`dli.s`](dli.s) — the display-list-interrupt handler (per-scanline `COLBK`),
  shared by the title sky and the in-game board sheen.
- [`csp_compat.s`](csp_compat.s) — the shared cc65 `c_sp`/`oserror` shim (also used
  by the C64/Apple II online builds).

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

## Display / input / sound (implemented)

A **horizontal** board (3 rows × 8 columns: top = Light, middle = the shared capture
lane, bottom = Dark — the authentic Standard-of-Ur orientation, matching the SMS and
every other port), drawn in **ANTIC mode 4** (multicolour charset) inside the mode-4
band (screen char rows 3–18), with a text HUD on the mode-2 rows above/below.

- **Board:** a custom character set draws every square as a 16×16 (2×2-char) inlaid
  mosaic — **carved, beveled lapis tiles** with a **gold bullseye eye** down the
  shared lane and a **white-stud quincunx** on the private lanes, plus **gold 8-point
  flower rosettes** at the 5 rosette squares (gold + white pearl, via inverse-video →
  `COLOR3`). The field is a deep lapis. (Glyphs: `tools/atari-mosaic-glyphs.c`.)
- **Living-tablet sheen:** an in-game **DLI** (`atari_board_dli_on`, reusing
  `dli.s`) grades the field colour (`COLBK`) down the board band — dark-blue framed
  edges easing to a luminous lapis body — the Atari's signature per-scanline colour.
- **Tokens: charset discs.** On-board pieces are round 16×16 (2×2-char) charset
  discs — **white** Light / **green** Dark (`tools/atari-token-glyphs.c`), drawn in
  the cell. The horizontal board rules out the old PMG tokens (a P/M player is one
  *vertical* column strip, so it can't render a row's several tokens at different X) —
  so, like the C64 dropping its sprites for the dense board, the A8 now uses charset
  tokens, unified with the 5200. Off-board tray stacks are small charset beads.
- **Dice:** four tetrahedral dice that **tumble** through random faces (POKEY RNG)
  then settle on the roll (`dice_tumble`); monochrome on the text HUD row.
- **Cursor:** a gold charset pointer left of the selected cell (`cursor_at`).
- **Title:** a Sumerian ziggurat with a DLI lapis→gold sky gradient.
- **Input:** joystick (port 1) to move the cursor + FIRE to select; keys `1`–`N`
  also pick a move; keyboard for the menu.
- **Sound:** POKEY effects for roll / move / capture / rosette / bear-off / win.

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
- **Run:** load the `.xex`/`.atr` in **Altirra**, or **`atari800 -windowed -nobasic
  build/atari/ur.xex`** (native Linux — good for quick local-play / visual checks;
  it has no FujiNet).
- **Network test:** Altirra + **FujiNet-PC**, or real FujiNet hardware on a real Atari
  (AltirraSDL/atari800 have no FujiNet — use Wine Altirra or FujiNet-PC for online).

> `cc65`, MADS, and an emulator (Altirra/atari800) are developer prerequisites and
> are not assumed installed in this environment.

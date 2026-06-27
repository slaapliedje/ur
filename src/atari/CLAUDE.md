# src/atari — Atari 8-bit platform layer (primary target)

The Atari 8-bit (400/800/XL/XE) implementation of the platform interface. This is
the **first and primary** target, and the **visual reference** the other ports aim
toward (see [`docs/visual-design.md`](../../docs/visual-design.md)). Local hot-seat,
vs-AI, and FujiNet online all work, with the full visual treatment in place.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` interface declared by [`src/common`](../common/CLAUDE.md). Keep all
> Atari-specific code here; never push hardware details into `common/`.

## Files

- [`main.c`](main.c) — game flow, the vertical board renderer (`draw_all`), move
  selection, the glide/fly-back + dice-tumble animations, and the FujiNet online loop.
- [`atarihw.c`](atarihw.c)/[`.h`](atarihw.h) — the hardware layer: colour registers,
  the custom charset (carved tiles, gold-flower rosettes, dice, cursor glyphs), the
  mode-4 board display-list patch, the in-game **DLI** field sheen, the **PMG** token
  discs (+ glide helpers), POKEY sound effects, and joystick input.
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

A **vertical** board (8 rows × 3 columns: left = Light, middle = the shared capture
lane, right = Dark), drawn in **ANTIC mode 4** (multicolour charset) on screen rows
4–18, with a text HUD on the mode-2 rows above/below.

- **Board:** a custom character set draws **carved, beveled lapis tiles** (a white
  top/left bevel on a lapis face) and **gold 8-point flower rosettes** (gold petals +
  a white pearl centre, via inverse-video → `COLOR3`). The field is a deep lapis.
- **Living-tablet sheen:** an in-game **DLI** (`atari_board_dli_on`, reusing
  `dli.s`) grades the field colour (`COLBK`) down the board band — dark-blue framed
  edges easing to a luminous lapis body — the Atari's signature per-scanline colour.
  All field shades stay darker than the tiles so the carve keeps its relief.
- **Tokens: player/missile graphics.** On-board pieces are round two-tone **PMG**
  discs (cream Light, brown Dark; a centre hole shows the pip — the dark field for
  Light, a cream charset dot for Dark). The vertical layout lets one player cover a
  column, so no multiplexing: P0 = col0 Light, P1 = col2 Dark, P2/P3 = the shared
  middle by colour. Off-board tray stacks stay charset glyphs.
- **Motion:** moves **glide** cell-by-cell along the path (`anim_glide`); a capture
  knocks the victim back along its own path to the tray. Local play only (online
  renders server snapshots directly). Tunable via `ANIM_STEP`/`ANIM_FLY`.
- **Dice:** four tetrahedral dice that **tumble** through random faces (POKEY RNG)
  then settle on the roll (`dice_tumble`); monochrome on the text HUD row.
- **Cursor:** a gold charset pointer left of the selected cell (`cursor_at`) — the
  four players are all tokens, so the cursor isn't PMG.
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

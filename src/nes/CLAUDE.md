# src/nes — NES / Famicom platform layer (6502)

> **Status: bring-up — local play (hot-seat + vs-AI), conio board + controller.**
> `src/nes/main.c` reuses the shared core unchanged and renders the **horizontal
> 3×8 Standard-of-Ur board** with the cc65 `nes` conio (a built-in font flushed to
> the PPU in vblank) on a lapis field: `**` gold rosettes (corners + shared centre),
> `()` bullseye eyes down the shared lane, `..` quincunx on the private lanes, and
> two-tone tokens (`OO` Light / `XX` Dark). The NES controller drives everything
> (read straight off `$4016`). `make nes` → `build/nes/ur.nes` (an iNES NROM cart);
> run in MAME (`nes`) / Mesen / FCEUX. **Verified in MAME `nes`:** menu, the board,
> roll, the move list with a D-pad selector, and moves applying (a token appears,
> the turn passes, the AI replies).
>
> **There is NO FujiNet for the NES** (it isn't in fujinet-lib's target list), so
> this is a **local-only** build, like the ColecoVision cartridge — no online path.
>
> **Next pass (make it pretty, like the other ports):** replace the conio text cells
> with **custom CHR-ROM tiles** — carved 2×2-tile (16×16) rosette/eye/quincunx cells
> and round two-tone tokens, with a lapis/gold/cream/brown palette — driven by direct
> PPU/nametable writes instead of conio. The NES is a tile+sprite machine (like the
> SMS/Adam), so the dense-mosaic-over-sprites rule applies: draw the tokens as
> background tiles (NES has the same per-scanline sprite limit), not as OAM sprites.

Implements the platform layer for the **NES / Famicom**.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). The shared core it builds on:
> [`src/common`](../common/CLAUDE.md).

## Hardware

- **CPU:** Ricoh **2A03** — a 6502 core (no decimal mode) @ ~1.79 MHz (NTSC). Built
  with **cc65** (`-t nes`), so the shared C core drops straight in, like the other
  6502 targets.
- **PPU (2C02):** tile/sprite video — a 256×240 background from two pattern tables
  (CHR), 4 background + 4 sprite palettes (3 colours each + a shared backdrop), and
  64 sprites (8 per scanline). VRAM is reached through the `$2006`/`$2007` ports.
- **APU:** 2 pulse + triangle + noise + DPCM. (No sound yet — see below.)
- **RAM:** 2 KB internal. cc65's `nes.cfg` also uses **8 KB cartridge PRG-RAM at
  `$6000`** for `DATA`/`BSS`/heap, so an emulator that maps NROM PRG-RAM is required
  (MAME/Mesen/FCEUX all do).
- **Cartridge:** this build is **mapper 0 (NROM)** — 16 KB PRG + 8 KB CHR, the iNES
  layout cc65 emits.
- **No keyboard.** Input is the standard controller, read by strobing `$4016` (write
  1 then 0) and reading 8 bits: A, B, Select, Start, Up, Down, Left, Right.

## conio + the controller (bring-up renderer)

cc65's `nes` conio gives text out of the box: it owns the PPU, ships an 8×8 font in
CHR-ROM, and an **NMI handler flushes a write buffer to VRAM each vblank**, so
`clrscr`/`cputsxy`/`gotoxy`/`cputc`/`textcolor`/`bgcolor` all work. `waitvsync()`
paces frames. `bgcolor(COLOR_BLUE)` paints the lapis field; per-cell `textcolor`
distinguishes the motifs (colour fidelity is limited by conio's single palette).

Input is read directly (`$4016`), since the NES has no `cgetc`:

- **`read_pad()` is DMA-safe.** The conio NMI (PPU flush / OAM DMA) can fire
  mid-read and duplicate/drop a controller bit, returning phantom presses — a
  classic NES gotcha that left the move-selector wedged (a phantom "held" bit means
  the wait-for-release never completes). The fix: re-read until two consecutive
  reads agree.
- **Edge-triggered input.** `wait_pad()` waits for a full release, then the next
  press — so one tap = one action. Menu = A (two players) / B (vs computer); roll /
  continue prompts = any button; the move list is a **D-pad Up/Down selector +
  A to confirm**.

## Build & run

- **Build:** `make nes` (`makefiles/nes.mk`) → `cl65 -t nes -O` →
  `build/nes/ur.nes` (iNES NROM: 16 KB PRG + 8 KB CHR).
- **Run:** `mame nes -cart build/nes/ur.nes -window -skip_gameinfo` (also Mesen /
  FCEUX / real hardware via a flash cart). MAME shows a one-time "known problems"
  warning — press a key to dismiss it.
- **Driving it in the X11/MAME harness (DISPLAY=:0):** window class `mame`
  (`xdotool search --class mame`); `import -window <wid>` screenshots. MAME's
  default NES keys are **A = Left-Ctrl, B = Left-Alt, Start = `1`, Select = `5`,
  D-pad = arrows**. Note A/B are X11 *modifier* keys, so drive them with explicit
  `keydown`/`keyup` (never leave one stuck) and hold each ~0.3 s so the once-per-
  frame poll catches it — a real controller has no such issue. **Launch MAME inside
  a background task and keep the input/screenshot steps in that same task** (a
  `&`-launched MAME in a foreground call is killed when the call returns).

## When you continue this port

1. The `plat_*`/core fit is already proven — the shared core compiles and plays.
2. Add **CHR-tile graphics** (see "Next pass" above): a custom CHARS bank + palettes
   + direct nametable writes for the carved Standard-of-Ur board, replacing conio.
3. Add **APU sound** (the Hurrian Hymn + sfx) via the pulse channels, like the other
   ports' chip players.
4. No networking — the NES has no FujiNet; keep it local-only.

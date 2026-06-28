# src/sms — Sega Master System platform layer (offline port)

> **Status: renders + playable, with colour + sound — local hot-seat + vs-AI.**
> `src/sms/main.c` reuses the shared core and draws the H-shaped 3×8 board via VDP
> **Mode 4**: **gold** title + rosettes, **two-tone disc tokens** (cream Light, red
> Dark with a cream pip), white lane (`.`) cells, Light/Dark tray stacks, a Turn/Roll
> HUD, and a D-pad move chooser. **SN76489 sound**: the Hurrian Hymn at boot
> (skippable) + roll/move/capture/rosette/score/win effects. Control-pad input
> (D-pad + button 1). No FujiNet (a cartridge console). `make sms` →
> `build/sms/ur.sms` (run in MAME `sms` / Emulicious). `make gamegear` builds the
> same code for the Game Gear (SMS-family VDP).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` contract from [`src/common`](../common/CLAUDE.md). Like the Adam, this is
> a **Z80** machine built with **z88dk** — different toolchain + asm from the 6502
> targets, but the shared C core drops in unchanged.

## Hardware

- **CPU:** Zilog Z80 @ ~3.58 MHz.
- **RAM:** 8 KB work RAM (tiny — smaller than the Adam's 64 KB; budget tightly).
- **Video:** SMS VDP (a Mode-4 superset of the TMS9918) — a **4bpp tilemap** (the
  "name table", 32×28, default base `$3800`), 16-entry BG + 16-entry sprite CRAM
  palettes, hardware sprites. Mode 4 is **not** a TMS9918 text mode — plain conio
  does not set it up (see Gotchas).
- **Sound:** SN76489 PSG (same chip family as the Adam/ColecoVision), driven
  directly at write-only I/O port **`$7F`** — see `sound.c` (a near-verbatim port of
  `src/adam/sound.c`; only the port differs, `$7F` vs `$FF`).
- **Input:** two D-pad + 2-button control pads, read at I/O port `$DC`/`$DD`.

## Rendering: the classic `<sms.h>` Mode-4 API (important)

We render with z88dk's **classic** `<sms.h>` VDP API (`load_palette`, `load_tiles`,
`set_bkg_map`, `read_joypad1`, `vdp_set_reg`, `clear_vram`) — **not** SMSlib
(devkitSMS) and **not** the newlib `<arch/sms.h>` terminal. Reason: this z88dk
install only ships the **classic** `sms_clib` (`lib/clibs/sms_clib.lib`); the
devkitSMS `sms.lib` and the newlib `sms.lib` are **not packaged**, and only `sccz80`
is installed (no `zsdcc`/SDCC). So `-clib=new`/`-clib=sdcc_iy` fail to link
(`file not found: sms.lib`); the default `-clib=default` links `sms_clib` + the
Mode-4 CRT. `makefiles/sms.mk` therefore uses plain `+sms` (default clib/startup).

Pipeline in `main.c`:
- `video_init()` — `clear_vram`; load **two CRAM palettes** (bank 0: field / white /
  cream / red; bank 1: field / gold); `load_tiles(font8, 0, 96, 1)` expands our 8×8
  **1bpp** font (`font8.h`, the printable slice of z88dk's `FONT8.BIN`) into 4bpp
  tiles (tile N = ASCII 0x20+N); then `load_tiles(disc_tiles, 96, 2, 4)` loads two
  hand-authored **4bpp disc tokens** (cream Light, red+cream-pip Dark); enable display.
- `put_ch`/`put_str`/`put_u` build name-table words (tile = char − 0x20, OR'd with
  the current `ink`) and `set_bkg_map(...)` them at (x,y); `put_tile` places a raw
  tile (the disc tokens). `screen_clear()` fills the name table row by row.
- **Two inks from one font.** `set_ink(INK_GOLD)` ORs the name-table
  `BKG_ATTR_SPRPAL` bit, which makes a tile use palette **bank 1** — so the same
  white font tiles render **gold** (the title + rosettes) without a second font copy.
  The disc tokens get true cream/red from their own colour indices (2/3) in bank 0.

## Gotchas (SMS-specific, non-obvious — all hit during bring-up)

- **conio black-screens.** The classic console runs a TMS9918-style text mode and
  defaults to an **invisible ink** (palette entry 1 = `0x00`); `clrscr`/`cputs`
  alone showed nothing. The fix is to drive Mode 4 directly with `<sms.h>` and set a
  visible palette ourselves.
- **The CRT leaves the display OFF for Mode 4.** `DefaultInitialiseVDP` writes VDP
  R1 = `0x80`, which does **not** set the SMS display-enable bit (**bit 6**, `0x40`).
  We must `vdp_set_reg(0x01, 0xC0)` to turn the screen on. `display_off()`/
  `display_on()` (R1 `0x80`/`0xC0`) also bracket each `draw_board` so the full
  redraw happens blanked — **avoids tearing** (writing the name table during active
  display otherwise shows a torn mix of the old + new frame).
- **sccz80 miscompiles `cond ? f() : g()` when both arms are function calls.** The
  turn loop `over = (player==1 && ai1) ? computer_turn() : human_turn()` ran
  `computer_turn` even on the human's turn (board showed "Computer's turn" with a
  LIGHT header on turn 0). Use an explicit `if/else`. (The Adam build happens to get
  away with the ternary; the SMS build did not — don't rely on it.)
- **The control port floats "pressed" at reset.** A remembered-previous-state edge
  detector counts that as input and blows through the menu/prompts. Use the Adam's
  **release-then-press** poll (`wait_press`: wait for all-released, then the next
  press) — one tap = one action, robust against the floating initial state.
- **`read_joypad1()` returns the buttons in the low byte only** (`in $DC` + `cpl`;
  a set bit = pressed, `JOY_UP`..`JOY_FIREB`). Mask with `& 0xFF` — the high byte is
  not cleared.

## Build & run

- **Build:** `make sms` → `build/sms/ur.sms` (z88dk `zcc +sms ... -create-app`;
  `-create-app` writes the SEGA ROM header). `make gamegear` → `build/sms/ur-gg.gg`.
- **Run:** `mame sms -cart build/sms/ur.sms -window -skip_gameinfo` (needs the MAME
  `sms` BIOS romset). Default MAME mapping: D-pad = arrow keys, **button 1 = Left
  Ctrl**.
- **Driving it headlessly** (X11 + xdotool + ImageMagick `import`, like the Atari
  `run-ur` skill): launch → `nap` (perl, not `sleep`) → `xdotool windowactivate
  --sync` → real-XTEST `keydown`/`keyup` → `import -window`. Run `xset r off` first
  so X **key auto-repeat** doesn't turn one keydown into several control-pad edges.
- **Verifying audio:** `mame sms -cart … -sound sdl -wavwrite out.wav` captures the
  emulated PSG stream; measure it with `ffmpeg -i out.wav -af volumedetect -f null
  /dev/null` (non-silence ⇒ the SN76489 writes are landing). With `-nothrottle` the
  capture runs far ahead of real time, so a few real seconds is plenty.

## When you extend this port

1. **Carved lane tiles** — give the lane cells a beveled/coloured tile (like the
   Adam's `carve_cell`) instead of a plain `.`, for closer visual parity. The token
   discs + gold rosettes are already custom 4bpp tiles; a lane tile is the same idea.
2. **Animation** — glide a token cell-to-cell on a move / knock a captured piece back
   (the Atari/Adam do this); the SMS could move a hardware sprite over the tilemap.
3. **In-game music / richer SFX** — currently the hymn is title-only; a per-frame
   PSG tick (vblank) could play under the board like a future Atari/Adam parity pass.
4. The core + protocol are unchanged; if FujiNet-for-SMS ever lands, the online
   path mirrors the other targets.

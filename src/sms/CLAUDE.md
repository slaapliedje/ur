# src/sms — Sega Master System platform layer (offline port)

> **Status: renders + playable — local hot-seat + vs-AI.** `src/sms/main.c` reuses
> the shared core and draws a text board (white-on-lapis) via VDP **Mode 4**: the
> H-shaped 3×8 board with rosette (`*`) and lane (`.`) cells, `O`/`X` pieces, Light/
> Dark tray stacks, a Turn/Roll HUD, and a D-pad move chooser. Control-pad input
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
- **Sound:** SN76489 PSG (same chip family as the Adam/ColecoVision) — not wired up
  yet here (the Adam's `sound.c` port-0xFF approach is the model when we add it).
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
- `video_init()` — `clear_vram`; `load_palette` (CRAM 0 = dark-blue field, 1 =
  white) into both banks; `load_tiles(font8, 0, 96, 1)` expands our 8×8 **1bpp**
  font (`font8.h`, the printable slice of z88dk's `FONT8.BIN`) into 4bpp tiles
  (tile N = ASCII 0x20+N); then enable the display.
- `put_ch`/`put_str`/`put_u` build a small array of name-table words (tile = char −
  0x20) and `set_bkg_map(...)` them at (x,y). `screen_clear()` fills the name table
  with the space tile row by row.

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

## When you extend this port

1. Add **SN76489 sound** (model on `src/adam/sound.c` — direct PSG writes; the SMS
   PSG is at I/O port `$7F`, or use the `<sms.h>` `set_sound_freq`/`set_sound_volume`).
2. Consider **hardware sprites** for round two-tone tokens (like the Adam) instead
   of `O`/`X` glyphs, and a carved tile board, for visual parity.
3. The core + protocol are unchanged; if FujiNet-for-SMS ever lands, the online
   path mirrors the other targets.

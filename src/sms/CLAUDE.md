# src/sms — Sega Master System platform layer (offline port)

> **Status: the graphical showpiece — playable, "Standard of Ur" art + sound.**
> The SMS is the most capable hardware of all the ports, so it gets the richest
> look. `src/sms/main.c` reuses the shared core and draws the **authentic horizontal
> H-board** (3 rows × 8 columns) via VDP **Mode 4** with a Standard-of-Ur palette
> (lapis field, gold, shell-white, carnelian): **carved beveled stone cells**, every
> square an inlaid mosaic — gold **8-point rosette** stars at the 5 rosette squares,
> bullseye **"eyes"** down the shared lane, **five-dot quincunx** studs on the private
> lanes — plus shaded round **tokens** (shell-white Light, lapis Dark with a shell
> rim + gold pip) that **glide cell-to-cell as a hardware sprite** on a move,
> shell/lapis bead **trays**, a gold title, a Turn/Roll HUD, a
> D-pad move chooser that **tints each legal destination cell green** (a recoloured
> copy of its motif — `greenify()` → `TILE_GROSE/GDOTS/GEYE` — so the rosette/eye/
> quincunx stays visible on green), and a **rosette-cursor difficulty menu**. The
> board/token/rosette tiles are **generated procedurally at
> boot** (a 16×16 colour grid → four 4bpp tiles), not stored as art. **SN76489
> sound**: the Hurrian Hymn (skippable) — played **after** the title screen is drawn
> (`title_menu()` calls `play_hymn()` post-draw) so the boot isn't a long blank —
> plus roll/move/capture/rosette/score/win effects. Control-pad input (D-pad +
> button 1). No FujiNet (a cartridge console).
> `make sms` → `build/sms/ur.sms` (run in MAME `sms`, **RetroArch + Genesis Plus GX**,
> or Emulicious). **`make gamegear`**
> → `build/sms/ur-gg.gg` builds the **same renderer** for the Game Gear: identical
> art/palette/tokens/animation, just a **compacted layout** (`-DUR_GG`) because the GG
> only shows a 160×144 (20×18-tile) window — see "Game Gear layout" below.
>
> Design direction (horizontal board, sprite-token plan, materials) is recorded in
> the project memory; see also the per-platform `CLAUDE.md` files for the other ports.

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
- `video_init()` — `clear_vram`; load **two CRAM palettes** (bank 0 = the Standard
  of Ur palette: field / white / lapis-face / highlight / shadow / gold / shell /
  carnelian / grey / dark-gold; bank 1 = field / gold for the gold ink);
  `load_tiles(font8, 0, 96, 1)` expands the 8×8 **1bpp** font (`font8.h`) into 4bpp
  text tiles (tile N = ASCII 0x20+N); then **procedurally builds** the board art.
- **Procedural tiles.** Each board cell is 16×16 = four 4bpp tiles. A builder fills a
  256-byte `grid[]` (16×16 colour indices) by maths — `build_carved` (beveled face),
  `build_rosette` (8-point gold star clipped to a radius), `build_dots` (quincunx via
  `stamp_dot`), `build_eye` (concentric ring), `build_token16` (shaded disc with a
  rim/highlight/shadow/pip), `build_bead8` (8×8 tray bead) — then `load_cell16`
  packs each 8×8 quadrant into a planar 4bpp tile (`load_quad`) and `load_tiles`-es
  it. This keeps the ROM tiny (no stored art) and makes the look easy to tune.
- **Layout.** Horizontal board, 16×16 cells at `cellx/celly` (tile origin BX=8,
  BY=8). `pos_to_cell` maps a path position to (row,col) with Light=row 0, shared=row
  1, Dark=row 2; `cell_exists` carves the H-shape; `is_rosette_cell` marks the 5
  rosette squares. `put_cell(x,y,first)` lays a 4-tile cell; `put_tile` a single tile.
- `put_ch`/`put_str`/`put_u` build name-table words (tile = char − 0x20, OR'd with
  the current `ink`) and `set_bkg_map(...)` them; `set_ink(INK_GOLD)` flips the
  `BKG_ATTR_SPRPAL` bit so the white font renders gold (the title). `screen_clear()`
  fills the name table with the space tile (= field colour) row by row.
- **Two inks from one font.** `set_ink(INK_GOLD)` ORs the name-table
  `BKG_ATTR_SPRPAL` bit, which makes a tile use palette **bank 1** — so the same
  white font tiles render **gold** (the title + rosettes) without a second font copy.
  The disc tokens get true cream/red from their own colour indices (2/3) in bank 0.

## Game Gear layout (`-DUR_GG`)

The Game Gear uses the **same VDP, palette, tiles, tokens and animation** — the only
difference is the visible area. The GG LCD shows just a **160×144 window = the
top-left 20×18 tiles** of the name table (verified empirically with a ruler probe:
visible columns 0–19, rows 0–17 under MAME's `gamegear` driver). The full SMS layout
(32×24) would clip the title's right end and the board's right block.

So one renderer drives two layouts via `#ifdef UR_GG`: a block of layout constants
(`BX/BY`, `TITLE_*`, `HUD_*`, `LTRAY_Y/DTRAY_Y`, `TRAY_WX/TRAY_HX`, `LIST_*`,
`MSG_*`, `LBL_*`) chosen at compile time, used everywhere in `draw_board` /
`choose_move` / `title_menu` / `cell_px`. The GG build compacts the board to cols
2–17 / rows 4–9, single-letter tray labels, a one-line HUD (`Turn:LIGHT Rl:N`), and
keeps every message ≤ 20 chars. `makefiles/sms.mk` adds `-DUR_GG` to the `gamegear`
target. **To re-find the visible window** (e.g. on another emulator), build with
`-DUR_GGPROBE`-style rulers and read which row/column digits are on screen.

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
- **Better rig for headless verification — RetroArch + Genesis Plus GX:**
  `retroarch -L /usr/lib/libretro/genesis_plus_gx_libretro.so build/sms/ur.sms
  --appendconfig <cfg>`. GPGX is accurate (renders the board/sprites correctly) and
  takes **plain keyboard input** — `z` = button 1/fire, arrows = D-pad, `Return` =
  Start — far less fiddly than MAME's Left-Ctrl. Two gotchas: (1) RetroArch **pauses
  when the window loses focus**, which fights scripted focus-switching — put
  `pause_nonactive = "false"` in the `--appendconfig` cfg; (2) the boot hymn's skip is
  only polled **between notes**, so a quick tap can miss it — **hold** fire ~2.5 s to
  skip. GPGX also does the Game Gear; it does **not** do the NES (use MAME `nes`), and
  `dgen-sdl` is Genesis-only (no `.sms`).
- **Driving it headlessly** (X11 + xdotool + ImageMagick `import`, like the Atari
  `run-ur` skill): launch → `nap` (perl, not `sleep`) → `xdotool windowactivate
  --sync` → real-XTEST `keydown`/`keyup` → `import -window`. Run `xset r off` first
  so X **key auto-repeat** doesn't turn one keydown into several control-pad edges.
  **Screenshot-timing note:** the move chooser is transient under fast input — capture
  right after the roll and *before* any confirm, or you'll snap the post-move board and
  wrongly conclude the highlight/sprite didn't render (this bit me).
- **Verifying audio:** `mame sms -cart … -sound sdl -wavwrite out.wav` captures the
  emulated PSG stream; measure it with `ffmpeg -i out.wav -af volumedetect -f null
  /dev/null` (non-silence ⇒ the SN76489 writes are landing). With `-nothrottle` the
  capture runs far ahead of real time, so a few real seconds is plenty.

Token glide animation uses hardware sprites (`anim_move`/`glide`/`put_token_sprite`):
the moving piece becomes a four-sprite 16×16 token (sprite palette = CRAM 16..31,
`TILE_SPRL`/`TILE_SPRD`) that slides cell-to-cell along the path, paced by
`wait_vblank_noint`; static pieces stay BG tiles so the per-scanline sprite limit is
never near. Source verified: a static sprite test (`-DUR_SPRTEST`) confirmed both
token sprites render with correct colours, and full games play correctly with
`anim_move` in the loop. (Single-frame *mid-glide* screenshots are timing-hard: under
MAME `-nothrottle` the glide flies by, and *with* throttle the busy-wait boot hymn
runs at real Z80 speed and dominates — so verify motion live, not by one capture.)

### Still to do
1. **Capture knock-back** — when a move captures, glide the victim back to its tray
   too (only the mover animates today; the capture just redraws).
2. **In-game music / richer SFX** — the hymn is title-only; a per-frame PSG tick
   (vblank) could play under the board, a future Atari/Adam parity pass.
3. The core + protocol are unchanged; if FujiNet-for-SMS ever lands, the online
   path mirrors the other targets.

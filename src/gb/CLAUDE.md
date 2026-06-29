# src/gb — Nintendo Game Boy / Game Boy Color platform layer (offline port)

> **Status: playable — ONE dual-mode cart.** `build/gb/ur.gb` runs on a **Game Boy
> Color in colour** (the "Standard of Ur" look: lapis field, gold, shell-white) and
> on a **plain DMG Game Boy in 4 greys**, from the same ROM. It reuses the SMS port's
> design — the authentic horizontal H-board (3 rows × 8 cols), procedurally drawn
> carved cells, gold **rosette stars**, bullseye **"eyes"**, five-dot **quincunx**
> studs, and two-tone **tokens** — on the GB's 160×144 (20×18-tile) screen. Local
> hot-seat + vs-AI; D-pad + A button. **APU sound** (the Hurrian Hymn title theme +
> event SFX), entropy-seeded dice, and a hardware-sprite **token glide** are all in.
> No FujiNet. `make gb` → `build/gb/ur.gb`.
> Run: `mame gameboy` (grey) / `mame gbcolor` (colour).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). The shared `src/common` core drops
> in unchanged. This is a **Sharp LR35902** (gbz80) — a third CPU/toolchain after the
> 6502 (cc65) and Z80 (z88dk SMS/Adam); the core is written to span all of them.

## Hardware

- **CPU:** Sharp LR35902 (a Z80-ish core, *not* a true Z80) @ ~4.19 MHz (GBC: ~8.4
  MHz double-speed, unused here).
- **Video:** 160×144, 8×8 **2bpp** tiles (16 bytes/tile), a 32×32 BG map (20×18
  visible at scroll 0). DMG: one 4-grey BG palette. **GBC: 8 BG + 8 sprite palettes**,
  4 colours each from a 32768-colour (15-bit RGB) master, per-tile palette via BG
  attributes (VRAM bank 1). 40 sprites (8×8/8×16), 10/scanline.
- **Sound:** 4-channel APU (regs `$FF10`+) — `src/gb/sound.c` uses channel 1
  (square) for the melody + melodic blips and channel 4 (noise) for the dice rattle.
- **Input:** D-pad + A/B + Start/Select (`joypad()` → `J_A`/`J_B`/`J_UP`/…).

## Toolchain (z88dk `+gb`)

z88dk's **`+gb`** target (gbz80 backend, `gb_clib`, `gbz80_crt0`) provides a
GBDK-style API: `set_bkg_data` (load 2bpp tiles), `set_bkg_tiles` (write the BG
map), `set_sprite_*`, `joypad`/`waitpad`/`waitpadup`, `DISPLAY_ON/OFF`, `SHOW_BKG`,
and **`<arch/gb/cgb.h>`** for GBC colour: `set_bkg_palette` / `set_sprite_palette` /
`RGB(r,g,b)`, plus `_cpu == CGB_TYPE` to detect a Color unit. **No GBDK install
needed.** (GBDK-2020 is the usual GBC toolchain; z88dk's `+gb` covers it here.)

## Colour: one 4-colour palette, dual-mode

The whole board uses **one 4-colour palette** — 0 field (dark lapis), 1 face
(lapis), 2 shell (cream/white), 3 gold — which is enough for the carved bevels
(field/face/shell) *and* the gold motifs + tokens. At boot we branch on the CPU:

- **GBC** (`_cpu == CGB_TYPE`): `set_bkg_palette(0,1, cgb_pal)` with 15-bit RGB →
  full colour. All tiles use BG palette 0, so no per-tile attribute layer is needed.
- **DMG**: write the `BGP` register (`0xFF47 = 0x4B`) so the four colour indices map
  to four distinct greys (shell brightest, field darkest) → the same tiles read as a
  clean greyscale board.

Tiles are **generated procedurally at boot** (a 16×16 colour grid baked into four
2bpp tiles — `grid_carved`/`build_rosette`/`build_dots`/`build_eye`/`build_token`/
`build_bead` + a 2bpp `load_quad`), exactly like the SMS port, so the ROM stays tiny.

## CGB-compatible cart header (the dual-mode key)

A GBC only enters colour mode if cart header **byte 0x143 = 0x80** ("CGB enhanced,
DMG-compatible"). z88dk's gb crt0 hard-codes it to 0 (DMG-only), so
**`makefiles/gb-cgb-patch.pl`** (run by the `gb` target after the build) sets
`0x143 = 0x80` **and recomputes the header checksum at 0x14D** — without that fix the
GBC/DMG boot ROM refuses to start. (0xC0 would be CGB-only; we want 0x80 for both.)

## Gotchas (hit during bring-up)

- **`set_bkg_tiles` mis-handles a small multi-tile block.** Writing a 2×2 (or 2×1)
  cell from a tiny local array scattered the tiles (only the first landed, plus
  garbage). Single-tile writes are reliable, and a *large* array (the 20-byte text
  buffer in `put_str`) is fine — it's specifically a tiny-local-array codegen quirk.
  So **`put_cell` places its four tiles with four `put_tile()` calls**, not one
  `set_bkg_tiles(...,2,2,...)`. (Isolation test: 4×`put_tile` drew a perfect cell;
  the `set_bkg_tiles` 2-wide version drew a broken one.)
- **Redraw with the LCD off.** z88dk's `set_bkg_tiles` isn't VRAM-safe during active
  display, so `draw_board` wraps the full redraw in `DISPLAY_OFF`…`DISPLAY_ON` (a
  brief blank per turn, like the SMS). Single-tile HUD/selector updates are fine.
- **`-create-app` builds a 32K (2-bank) ROM** — the "requires cartridge with 2 ROM
  banks" line is just a notice, not an error.

## Build & run

- **Build:** `make gb` → `build/gb/ur.gb` (`zcc +gb … -create-app`, then the CGB
  header patch). font8.h is shared from `src/sms` (`-I$(SRC_DIR)/sms`).
- **Run:** `mame gbcolor -cart build/gb/ur.gb -window -skip_gameinfo` (colour) or
  `mame gameboy …` (greyscale). Default MAME P1: D-pad = arrows, **A = Left Ctrl**,
  B = Left Alt, Start = 1.
- **Headless driving:** like the SMS — `xset r off` first, launch → `nap` (perl) →
  `xdotool` real-XTEST keys → `import`. **The `gameboy` driver pops a "imperfectly
  emulated: sound" warning** that `-skip_gameinfo` doesn't dismiss — send one key
  (e.g. `Return`) after boot before driving the game.

## Sound (the LR35902 APU)

`src/gb/sound.c` drives the APU directly (regs `$FF10`–`$FF26`): **channel 1**
(square) plays the shared **Hurrian Hymn** (`gb_music_note`, an 11-entry frequency
table for the B4–A5 melody range) + the melodic blips, and **channel 4** (noise)
the dice rattle / capture buzz. Constant volume (no envelope); a channel is silenced
by turning its DAC off + retrigger. Durations are counted in frames by polling the
**LY** scanline register (`$FF44`) for vblank — so the **LCD must be on** while
sound plays, which it is on the menu/board (`draw_board` leaves it on). Same event
hooks as the other ports: `gb_sound_init()` at boot, a skippable `play_hymn()` on
the title, `sfx_roll()` after a roll, `sfx_for_result()` after a move.

**RNG seeding:** the dice are seeded once from entropy gathered in `play_hymn`
(mixing the free-running **DIV** timer `$FF04` per note) plus DIV sampled at the
human-timed moment the player confirms the menu — replacing the old fixed seed
(which made every game roll the same). The GB's blocking `waitpad` offers no idle
loop to count in, so the hymn loop is the GB's equivalent of the other ports'
input-timing accumulator. (When the game loop is lifted behind `plat.h`, this
becomes a standard `plat_` entropy hook.)

## Token glide (`plat_animate`)

The moving token slides cell-to-cell with **hardware sprites**, like the SMS. Four
8×8 sprites form the 16×16 token; `plat_animate(player,from,to)` clears the source
cell's BG token (a brief `DISPLAY_OFF`/redraw, same as `draw_board`), then glides the
sprite quad from the source pixel to the destination at 4 px/frame, pacing each step
by polling **LY** (`gb_waitframe`), and finally hides the sprites so the redrawn BG
token takes over. Sprite tiles live at the **$8000** base (separate from the BG's
signed **$8800** base set by the crt0), so the token is copied into sprite VRAM via
`set_sprite_data`; GBC gets a token sprite palette (`set_sprite_palette`), DMG sets
`OBP0`. The crt0 `vbl` ISR runs the OAM DMA (`refresh_OAM`), so `enable_interrupts()`
is required for `move_sprite` to take effect.

Gotchas: sprite color 0 is transparent and OAM coords are offset (`x+8`, `y+16`);
`set_interrupts` is **not** in this z88dk lib (the crt0 already enables the VBL IE —
use only `enable_interrupts()`). Like the SMS slide, the mid-glide frame is transient
and hard to screenshot — verify it live (the game plays through many turns without
hang/corruption), not by a mid-motion capture.

## Still to do

1. The core + protocol are unchanged; FujiNet isn't applicable to a GB cart.

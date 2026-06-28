# src/gb — Nintendo Game Boy / Game Boy Color platform layer (offline port)

> **Status: playable — ONE dual-mode cart.** `build/gb/ur.gb` runs on a **Game Boy
> Color in colour** (the "Standard of Ur" look: lapis field, gold, shell-white) and
> on a **plain DMG Game Boy in 4 greys**, from the same ROM. It reuses the SMS port's
> design — the authentic horizontal H-board (3 rows × 8 cols), procedurally drawn
> carved cells, gold **rosette stars**, bullseye **"eyes"**, five-dot **quincunx**
> studs, and two-tone **tokens** — on the GB's 160×144 (20×18-tile) screen. Local
> hot-seat + vs-AI; D-pad + A button. No FujiNet. `make gb` → `build/gb/ur.gb`.
> Run: `mame gameboy` (grey) / `mame gbcolor` (colour). Sound and token glide
> animation are follow-ups (see below).

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
- **Sound:** 4-channel APU (different from the SN76489) — not wired up yet here.
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

## Still to do

1. **Sound** — the GB APU (4 channels, regs at 0xFF10+); the hymn + SFX, a fresh
   layer (the SN76489 `src/sms/sound.c` doesn't apply).
2. **Token glide animation** — GB hardware sprites (`set_sprite_*`/`move_sprite`),
   like the SMS; would also enable a colour token sprite palette on GBC.
3. The core + protocol are unchanged; FujiNet isn't applicable to a GB cart.

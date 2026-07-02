# src/st — Atari ST platform layer (the first 16-bit / 68000 port)

> **Status: playable.** `make st` → `build/st/ur.prg`, a GEMDOS executable built with
> **m68k-atari-mint-gcc**, verified in **Hatari** (EmuTOS): the carved Standard-of-Ur
> board (gold rosettes, bullseye eyes, quincunx studs, two-tone disc tokens) drawn
> straight to the Shifter's planar low-res bitmap, hot-seat + vs-AI (Easy/Normal/Hard)
> via the shared `plat.h` controller, keyboard input, and **YM2149 sound** — the
> **Hurrian Hymn** title theme (verified by recording: the F5-E5-D5-C5-B4 descending
> tetrachord) + roll/capture/rosette/win SFX. The shared `src/common` core compiles
> **unchanged** under GCC for the 68000 — the same brain as the 6502 (cc65) and Z80
> (z88dk) ports, now on a third CPU family.
>
> **Enhanced Atari Falcon edition: playable** — `make st FALCON=1` →
> `build/st/ur-falcon.prg`, a **320×200 truecolor** (RGB565 chunky) build with
> **gradient-lit cell faces** (the flourish the ST's 16 colours can't do). Same
> `src/st/main.c`, same shared controller/geometry/sound/input — only the pixel format
> and palette differ (`#ifdef UR_FALCON`). Verified in Hatari `--machine falcon`.
> **Still planned: STe (4096-colour palette) and TT (256-colour) variants**, plus
> STe/Falcon DMA sound + the blitter.
>
> **Title scene + 3D board (both builds):** the title (and win screen) is a
> procedural **Great Ziggurat of Ur at dusk** — `title_scene()`/`zbox()` draw
> oblique-projection brick terraces (lit front / sand ledge / shaded side, mud-brick
> coursing), the grand stair, a blue-glazed shrine with a gilded door, a setting sun
> with halo, and stars; the Falcon gets a gradient night→amber sky, the ST a banded
> dusk from palette spares (idx 10/11/13–15 = shadow, dusk amber, brick, lit brick,
> sand). The board is a **raised slab**: a dark drop shadow along the H silhouette's
> south/east edges (strips only where no neighbour cell would cover them —
> `cell_exists` is unbounded, so board edges are guarded explicitly), crisp white cell
> rims + dark seat lines, and tokens/beads with offset drop shadows + specular glints.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). The Atari led our 8-bit era; the ST
> leads the 16-bit era too.

## Hardware

- **CPU:** Motorola 68000 @ 8 MHz (16/32-bit) — a real C target, lots of RAM (512K+).
- **Video (Shifter):** low-res **320×200, 16 colours** from a 512-colour palette (ST;
  4096 on STe), medium 640×200×4, high 640×400 mono. The bitmap is **word-interleaved
  4-bitplane planar** (each 16-px group = 4 consecutive 16-bit plane words; 160
  bytes/row). Palette regs at `$FFFF8240`; ST colour word = `0x0RGB`, 3 bits/channel.
- **Sound:** **YM2149 PSG** (`$FFFF8800`) — the AY-3-8910 family (same chip we drive on
  the Apple II **Mockingboard**), 3 square voices + noise + envelope. (STe adds DMA PCM.)
- **Input:** keyboard, mouse, joystick (read via the IKBD / BIOS).
- **OS:** TOS / GEMDOS; programs are `.prg`/`.tos` GEMDOS executables.

## Toolchain (m68k-atari-mint-gcc)

Plain **GCC** for the 68000 (the `m68k-atari-mint` cross-target). `m68k-atari-mint-gcc
-o ur.prg …` emits a runnable GEMDOS executable directly — no separate packer. OS/
hardware calls come from **`<osbind.h>`** (XBIOS/GEMDOS bindings: `Setscreen`,
`Setpalette`, `Physbase`/`Logbase`, `Cconws`, `Crawcin`, `Vsync`, …); `<gem.h>` has
VDI/AES if ever needed (we'll draw the board direct to the bitmap, not via GEM). Since
it's full GCC + plenty of RAM, the shared core drops straight in — far fewer
constraints than the 8-bit toolchains.

## Build & run

- **Build:** `make st` → `build/st/ur.prg` (`makefiles/st.mk`).
- **Run (Hatari):** needs a TOS image — EmuTOS is bundled at
  `/usr/share/hatari/etos512us.img`. Auto-run the .prg from a GEMDOS-emulated drive:
  ```sh
  hatari --tos /usr/share/hatari/etos512us.img --tos-res low --monitor rgb \
         --harddrive <dir-with-UR.PRG> --auto 'C:\UR.PRG' \
         --sound off --fast-boot on --confirm-quit off
  ```
  (Headless screenshots: `import -window $(xdotool search --class hatari|tail -1)`.)
- **Falcon build:** `make st FALCON=1` → `build/st/ur-falcon.prg`; run it as an RGB
  Falcon: `hatari --machine falcon --dsp none --monitor rgb --tos
  /usr/share/hatari/TOSv4.04.img --harddrive <dir> --auto 'C:\UR.PRG'`. Truecolor mode
  gotcha: set the depth with `VsetMode(BPS16|COL40)` **first**, THEN point the base at
  your buffer with `VsetScreen(buf,buf,-1,-1)` — `VsetScreen`'s mode arg alone won't
  change the bit-depth (you get planar stripes). Monitor reports MON=1 (STcolor/RGB).
- **Run (MAME):** the `st`/`megast` drivers also work.

## How it's built (`src/st/main.c`)

- **Video:** `Setscreen(-1,-1,0)` (low-res) + `Setpalette` (the lapis/gold scheme).
  Drawing primitives over `Physbase()`: `frectw` (fast 16-px-aligned fills — cell
  faces, screen clear), `pix`/`frect`/`disc`/`diamond` (motifs/tokens), and a `glyph`
  blitter for the shared **font8** (1bpp 8×8). The board redraws fully each
  `plat_draw` (fine for a turn-based game; double-buffering is a possible polish).
- **Geometry:** the shared `cell_exists`/`pos_to_cell`/`is_rosette_cell` layout every
  port uses. Cells 32×32 at 16-px-aligned offsets (so face fills are word-aligned/fast).
- **Input:** keyboard via `Crawcin` (no echo) + `Cconis` (the RNG entropy accumulator,
  `plat_seed`). Number-key menus + move picks (like the Atari 8-bit/C64/Apple II).
- **Sound:** YM2149 via XBIOS `Giaccess(val, reg|0x80)` (no supervisor needed); note
  timing via `Vsync`. Period table for 2 MHz PSG. `st_music_note` plays the shared
  `ur_hymn`; `sfx_*` cover roll/move/capture/rosette/score/win.
- Uses the shared controller `ur_game.c` (`$(UR_GAME_SRC)` in `st.mk`).

## What's next (polish / the enhanced edition)

1. **STe / TT / Falcon enhanced edition** — richer palettes (STe 4096, Falcon
   truecolor), STe DMA sound, blitter-accelerated drawing. (Planned; the user wants it.)
2. Optional base-ST polish: double-buffered redraw (no flicker), joystick input,
   nicer motifs, a dice-roll/token-glide animation (`plat_animate` is currently a stub).

# src/st — Atari ST platform layer (the first 16-bit / 68000 port)

> **Status: bring-up scaffold.** `make st` → `build/st/ur.prg`, a GEMDOS executable
> built with **m68k-atari-mint-gcc**, verified booting in **Hatari** (EmuTOS): it sets
> low-res, loads the Standard-of-Ur **lapis/gold palette**, and shows the title. The
> shared `src/common` core (rules/AI/proto/music) compiles **unchanged** under GCC for
> the 68000 — the same brain as the 6502 (cc65) and Z80 (z88dk) ports, now on a third
> CPU family. **Next:** the carved Standard-of-Ur board (planar bitmap), YM2149 sound,
> input, and the shared `plat.h` controller.

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
- **Run (MAME):** the `st`/`megast` drivers also work.

## What's next

1. **Carved Standard-of-Ur board** drawn to the planar low-res bitmap (the look every
   port shares) — needs a planar rect/blit helper (set the 4 plane words per 16-px cell).
2. **YM2149 sound** + the Hurrian Hymn (reuse the AY register approach from the Apple II
   Mockingboard player — same chip family).
3. **Input** (keyboard/joystick) and the shared **`plat.h`** controller (`ur_game.c`),
   so the ST joins the unified local game loop (add `$(UR_GAME_SRC)` to `st.mk`).

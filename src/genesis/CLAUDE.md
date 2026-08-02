# src/genesis — Sega Mega Drive / Genesis platform layer (offline port)

> **Status: playable — the SMS showpiece look on 16-bit hardware.** The second
> 68000 target after the Atari ST family, and the first built with **SGDK**.
> `src/genesis/main.c` reuses the shared core/controller and draws the authentic
> horizontal H-board with the *Standard of Ur* treatment in 9-bit colour: the
> **carved board lives on plane B** (drawn once), **resting tokens are
> transparent-cornered tiles on plane A** floating over the stonework (so the
> carved cell shows around each disc and nothing needs re-drawing when a token
> leaves), and the **moving piece glides as a 16x16 hardware sprite**. Move
> destinations tint green with the motif kept visible (`greenify()`), the move
> list flags captures/rosettes, and the difficulty menu uses the gold-rosette
> cursor — full feature parity with the other tile ports. **Sound**: the
> Hurrian Hymn plays on a **YM2612 FM plucked-lyre voice** (registers programmed
> directly from the 68000 — no XGM/VGM assets; each note retriggers the
> envelope so held notes ring and decay like a string; skippable, polled
> **every frame**), while SFX stay on the SN76489 PSG — the SMS's exact chip at
> the exact clock, so `sound.c`'s PSG half is `src/sms/sound.c` ported to
> SGDK's `PSG_write()` with identical tuning. No FujiNet (cartridge console).
> `make genesis` → `build/genesis/ur.bin`.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` contract from [`src/common`](../common/CLAUDE.md) via the shared
> controller (`ur_game.c`). Art is **procedural** (the SMS 16×16 colour-grid
> builders, repacked for the Genesis's linear 4bpp tiles) — no image files, no
> rescomp resources anywhere in the build.

## Hardware / SGDK usage

- **CPU:** 68000 @ 7.67 MHz. **VDP:** two scrolling tile planes (BG_A over
  BG_B), 4bpp tiles, 4 × 16-colour palette lines (9-bit RGB), 80 sprites.
  H40 mode: 320×224 = 40×28 tiles. **Sound:** YM2612 FM + **SN76489 PSG** (we
  use the PSG only, for SMS parity; FM is a future upgrade).
- **SGDK 2.11** (pinned): `VDP_loadTileData` / `VDP_setTileMapXY` /
  `VDP_clearTileMapRect` for tiles, `VDP_drawText` + `VDP_setTextPalette` for
  text, the **low-level sprite API** (`VDP_setSpriteFull` + `VDP_updateSprites`
  — not the Sprite Engine, which wants rescomp `SpriteDefinition`s), `PSG_write`
  for sound, `JOY_readJoypad`, `SYS_doVBlankProcess` for vsync + input refresh.
- **Palettes:** PAL0 = board/white-ink, PAL1 = sprite tokens/gold-ink. The SGDK
  font draws glyphs with **colour index 15**, so `VDP_setTextPalette(PAL0|PAL1)`
  is the white/gold ink switch (the Genesis twin of the SMS `BKG_ATTR_SPRPAL`
  trick).

## Gotchas (all hit during bring-up)

- **SGDK vs `<stdint.h>`/`<stdbool.h>`:** SGDK's `types.h` typedefs `bool` (u8)
  with **no stdbool guard**, and `#define`s `int8_t → s8` etc. assuming the real
  stdint never follows. So: include `<genesis.h>` FIRST, then `#undef` the
  stdint remaps (`uint8_t int8_t … size_t ptrdiff_t`) before including `ur.h`
  (see the top of `main.c`/`sound.c`). Reversing the order breaks SGDK's
  `typedef u8 bool` with a hard error.
- **Sprite tiles are COLUMN-major** (TL, BL, TR, BR) — BG tiles are row-major.
  The token art is loaded twice: `load_cell16()` for the plane-A copies,
  `load_cell16_spr()` for the sprite copies (also different palette indices).
- **`SYS_doVBlankProcess()` is the input pump** — `JOY_readJoypad` only
  refreshes inside it, so every wait/poll loop must call it each iteration.
- **`main(bool hardReset)`** — SGDK's boot code calls it with an argument;
  `-Wno-main` is in the flags for this.
- **PSG envelope is inverted** (`PSG_ENVELOPE_MAX` = 0, attenuation 15 =
  silent) — irrelevant here since we drive raw `PSG_write` bytes, but easy to
  trip on if switching to the PSG_* API.
- **YM2612 rules:** the 68000 must **hold the Z80 bus** while touching the chip
  (`Z80_requestBus(TRUE)` — held forever in `snd_init()`; safe because SGDK
  boots the null Z80 driver and we never load another). Slot registers in
  $30–$9F are ordered **S1,S3,S2,S4** at +0/+4/+8/+12 — the lyre patch is
  *symmetric* (identical modulators, identical carriers, algorithm 4) partly to
  sidestep that trap. Write `$A4` (block+fnum-hi) **before** `$A0` (latch
  order). And an FFT-caught timbre lesson: a **MUL=2 modulator produces
  odd-only harmonics** (clarinet, not lyre) — the 1:1 MUL ratio gives the full
  harmonic series a plucked string needs.
- **Redraw tearing:** `plat_draw` brackets its full plane-A rebuild in
  `VDP_setEnable(FALSE/TRUE)` like the SMS `display_off/on` — writes during
  active display showed torn frames on the busier redraws.

## Build & run

- **Build:** `make genesis` → `build/genesis/ur.bin` (128 KB, padded +
  checksummed by SGDK's `sizebnd`). Needs SGDK 2.11 + a bare-metal m68k-elf GCC
  (NOT the mint one) — see [`docs/development.md`](../../docs/development.md)
  and `makefiles/genesis.mk` (`GDK` / `M68KELF_BIN` overrides). The makefile
  replicates SGDK's `makefile.gen` recipe (rom_head → sega.o → `md.ld` link →
  objcopy → pad) rather than adopting SGDK's fixed project layout; skips with a
  notice when SGDK is absent (so CI/release builds stay green).
- **Run:** BlastEm, RetroArch + Genesis Plus GX, `dgen`, or MAME
  (`mame genesis -cart build/genesis/ur.bin` — use `genesis`, the NTSC machine,
  not the PAL `megadriv`).
- **Headless rig (the one that works on this Wayland desktop):** run the
  emulator inside **Xvfb** and drive it there — no window focus fights, nothing
  flashes on the user's screen:
  `Xvfb :97 -screen 0 1024x768x24 &`, then
  `DISPLAY=:97 SDL_VIDEODRIVER=x11 mame genesis -cart … -window -video soft`;
  find the window with `DISPLAY=:97 xdotool search --name MAME`, focus with
  `xdotool windowfocus --sync`, tap keys with `keydown`/150ms/`keyup` (a bare
  `xdotool key` tap can fall between input polls), shoot with
  `import -display :97 -window <id>`. MAME key: **Start = `1`** (all of
  A/B/C/Start confirm, so `1` avoids the Ctrl/Alt modifier gotchas).
  RetroArch on the real display is a trap here: it opens a **Wayland** window
  even with `WAYLAND_DISPLAY` unset (it connects to the default `wayland-0`
  socket) — force `video_context_driver = "x"` — and an occluded XWayland GL
  window **stalls to ~1 fps on vsync** (`video_vsync = "false"`, pace by
  `audio_sync`). Even then keyboard focus never sticks under the compositor —
  hence Xvfb.
- **Audio check:** `mame genesis -cart … -video none -sound sdl -wavwrite
  out.wav -str 25 -nothrottle`, then ffmpeg `volumedetect` (the boot hymn makes
  it obviously non-silent).

### Still to do
1. **Capture knock-back animation** (same gap as the SMS port).
2. A raised-slab board shadow + richer backdrop à la the ST-family ports —
   the VDP has the colours for it.
3. FM **SFX** too, or a second FM voice harmonising the hymn — channel 0 is
   the only one in use.

# src/amiga — Commodore Amiga platform layer (offline port)

> **Status: playable.** `make amiga` → `build/amiga/ur` (AmigaDOS hunk executable)
> **+ `build/amiga/ur.adf`** (a self-booting disk built by amitools' xdftool).
> The third 16-bit port: a near-verbatim port of the **Atari ST layer**
> (`src/st/main.c`) to the ST's great rival — same 320×200 16-colour carved
> Standard-of-Ur board, raised-slab shadows, ziggurat title scene, number-key
> menus, and the shared `plat.h` controller. The Amiga's OCS palette is 4096
> colours — exactly the STe's space — so this build uses the **STe colour
> table** including the deep-ember dusk band (`C_DUSK2`). **Paula sound** via
> audio.device: the Hurrian Hymn (FFT-verified F5-E5-D5-C5-B4) + roll/capture/
> rosette/score/win SFX. Kickstart 1.3-safe (V33 calls only, `-mcrt=nix13`).
> Verified end-to-end in MAME `a500` (KS1.3): boot from ADF → title → menus →
> full vs-AI game with green destination markers.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Sibling reference:
> [`src/st/CLAUDE.md`](../st/CLAUDE.md) — when editing shared-look code here,
> check whether the ST layer wants the same change.

## How it's built

- **OS-friendly throughout** (no custom copper lists, no direct chip pokes):
  intuition `OpenScreen` (`CUSTOMSCREEN|SCREENQUIET`, 320×200×4, `ShowTitle`
  FALSE) + a borderless BACKDROP window for input, `SetRGB4` palette, direct
  writes to the screen's **separate bitplanes** (`scr->BitMap.Planes[p]`,
  `BytesPerRow` 40) — `pix()` RMWs a bit in each plane, `frectw()` fills whole
  words per plane (the ST's word-interleaved primitives, re-laid-out). A blank
  chip-RAM `SetPointer` hides the mouse. 320×200 (not 256) keeps one layout for
  PAL and NTSC; PAL just shows border below.
- **Input:** IDCMP `VANILLAKEY` on the backdrop window. `key_avail()` drains
  the port into a one-key pushback buffer — the `Cconis()` twin, so the hymn
  polls for a skip without eating the menu key. RNG entropy = `VBeamPos()`
  folded in at each keypress (beam position when a human presses is random).
- **Sound — the audio.device pattern:** allocate any one channel at OpenDevice
  (`ioa_Data` = channel mask array, `ln_Pri` 10), start **one endlessly-looping
  8-sample square wave at volume 0** (`CMD_WRITE`, `ioa_Cycles=0`), then every
  note/SFX is just `ADCMD_PERVOL` steering period+volume — the Paula twin of
  the ST's YM register pokes; silence = volume 0. Paula period =
  3546895 / (freq × 8). The wave lives in `AllocMem(…, MEMF_CHIP)`.
- Timing via `WaitTOF()` (13 frames per eighth-note ≈ the ST tempo).

## Toolchain / build gotchas (all hit during bring-up)

- **bebbo's amiga-gcc moved:** `bebbo/amiga-gcc` is gone from GitHub; the live
  repo is **`AmigaPorts/m68k-amigaos-gcc`** (GCC 6.5, includes NDK + libnix).
  `make all PREFIX=$HOME/dev/toolchains/amigaos -j$(nproc)` — no root needed.
- **gdb breaks the build** under a modern host GCC (`std::allocator::construct`
  gone). We don't need gdb: after binutils' `_done` stamp appears, fake the gdb
  stamp (`echo done > build-*/binutils/_gdb`) and re-run `make all`.
- `-mcrt=nix13` selects the KS1.3-compatible libnix startup. `CreatePort` /
  `CreateExtIO` need `<clib/alib_protos.h>` + `-lamiga`; library/device name
  strings want `(STRPTR)` casts (NDK protos take `UBYTE *`).
- **xdftool refuses to overwrite** an existing ADF — `rm -f` first (amiga.mk
  does). ADF recipe: `create + format "UR" ofs + boot install boot1x + write ur
  + makedir s + write startup-sequence s/startup-sequence` (the s-s just runs
  `ur`).

## Run / verify

- **MAME:** `mame a500 -flop1 build/amiga/ur.adf`. MAME calls the a500 driver
  "NOT WORKING" in a red warning screen (press a key to dismiss) — ignore it;
  KS1.3 boots the ADF and the game runs fine. On this desktop use the **Xvfb
  rig** (see `src/genesis/CLAUDE.md`): `Xvfb :97` + `DISPLAY=:97
  SDL_VIDEODRIVER=x11 mame …`, `xdotool windowfocus` + keydown/150ms/keyup
  taps, `import -display :97`.
- **Full redraws take ~1–2 s** on the emulated 7 MHz 68000 (per-pixel C into
  four planes) — a screenshot right after a keypress can catch a half-drawn
  board (a half-drawn rosette diamond looks like a triangle; that fooled me
  once). Wait ~5 s after each action before capturing.
- **Audio:** MAME `-wavwrite` + Escape to quit cleanly; if MAME is killed
  before finalizing, the WAV header is broken — `ffmpeg -i broken.wav fixed.wav`
  recovers it for FFT checks.
- Also runs in amiberry / FS-UAE / WinUAE (needs a Kickstart there) and real
  hardware from KS1.3 up.

### Still to do
1. **Blitter fills + double buffering** — the full-screen redraw is the slow
   spot; the blitter could do the cell faces nearly for free.
2. Paula 4-channel arrangement of the hymn (chords!) or a sampled lyre voice —
   the machine that invented tracker music deserves more than one square wave.
3. Token glide + dice animation (`plat_animate` is a stub, same as the ST).

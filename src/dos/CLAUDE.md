# src/dos — MS-DOS / IBM PC platform layer (offline port)

> **Status: playable — the fourth CPU family.** x86 real mode joins the 6502,
> Z80, and 68000. `src/dos/main.c` is the Atari ST layer ported to **VGA mode
> 13h** (320×200, 256 colours, one byte per pixel at A000:0000 — the shared
> layout size with the friendliest pixel format of any port) and upgraded with
> the **Atari TT build's gradient ramps**, which mode 13h's DAC does natively:
> a 64-shade night→dusk-amber sky, 32-shade lit-from-top cell faces, a 16-shade
> sand ramp. Sound is the **PC speaker** (PIT channel 2 square waves — 1-bit
> like the Apple II, but with a free-running timer instead of cycle counting):
> the Hurrian Hymn on the title, roll/capture/rosette/win SFX. Keyboard
> number-key input like the other computer ports; green destination
> highlights; ESC at the title exits to DOS. No FujiNet **yet** — but
> fujinet-lib has an `msdos` target built with this same toolchain, so DOS is
> a real candidate to become the *fifth online platform*. `make dos` →
> `build/dos/ur.exe` (a ~16 KB real-mode MZ executable).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). This layer implements the
> `plat_*` contract from [`src/common`](../common/CLAUDE.md) via the shared
> controller (`ur_game.c`). Art is procedural; the 8×8 font is shared from
> `src/sms` (`-I` in `dos.mk`).

## Hardware / API usage

- **CPU:** any 8086+ (compiled with default 8086 instructions, small model).
  **Video:** VGA mode 13h via `int86(0x10)`; palette via DAC ports
  `3C8h`/`3C9h` (6 bits/channel); framebuffer via a `__far` pointer from
  `MK_FP(0xA000, 0)`. **Sound:** PC speaker — PIT channel 2 (`43h`/`42h`,
  mode 3 square wave, divisor = 1193182/freq) gated by port `61h` bits 0–1.
- **Timing:** the VGA vertical-retrace flag (`3DAh` bit 3) — mode 13h refreshes
  at ~70 Hz, so `MUS_EIGHTH` is 15 frames (≈214 ms, matching the other ports'
  ~216 ms eighth).
- **Input:** Watcom `kbhit()`/`getch()` (BIOS-backed, no echo). The RNG seed
  accrues in the `kbhit()` wait loop, like the ST's `Cconis()` spin.

## Toolchain (Open Watcom v2)

Built with **owcc** (Watcom's POSIX-style driver): `-bdos -mcmodel=s -O2
-fno-stack-check` → a 16-bit real-mode MZ `.exe`. Install = untar
`ow-snapshot.tar.xz` anywhere (no root); `dos.mk` sets `WATCOM`, `INCLUDE`,
and `PATH` itself. Chosen deliberately: **fujinet-lib's `msdos` target is
built with the same wcc/wlib**, so a future online build links against it
without a second toolchain.

## Gotchas (all hit during bring-up)

- **16-bit `int`:** `y * 320` overflows a signed int for y ≥ 103 — every
  framebuffer offset must be computed as `(unsigned)y * 320u` (fits: max
  63999). This is the one real difference from the 68000 ports' arithmetic.
- **`-std=c89` hides Watcom extensions** (`_fmemset`'s prototype, `__far`
  semantics): compile with the default dialect, not a strict `-std` flag.
- **`_fmemset` is the fill primitive** — one call per scanline row makes
  `frectw`/`frect` a single flat helper (no planar split, no 16-px alignment
  rule; `frect` just aliases `frectw`).
- **DOSBox auto-exits** when the program given on its command line terminates
  — so ESC-quit makes `dosbox build/dos/ur.exe` come back on its own (handy
  for scripted smoke runs; no `-exit` flag needed).
- **The DOSBox window is titled after the DOS program** (`UR.EXE - …cycles…`),
  not "DOSBox" — `xdotool search --name 'UR.EXE'` on the headless rig.
- **SDL disk-audio pacing:** capturing audio with `SDL_AUDIODRIVER=disk`
  runs DOSBox ~2.2× faster than realtime (SDL 1.2's disk driver sleeps a
  fixed 10 ms per callback), and the "stereo" file interleaves a duplicated
  junk stream with the real one — analyse the **odd samples** by
  zero-crossing run-lengths, and trust interval *ratios*, not absolute Hz.
  (The hymn verified this way: periods 194.4/183.4/163.5/145.6/137.4 = the
  B4–C5–D5–E5–F5 tetrachord exactly.)

## Build & run

- **Build:** `make dos` → `build/dos/ur.exe`. Needs Open Watcom v2 at
  `~/dev/toolchains/watcom` (override `WATCOM_DIR`); skips with a notice when
  absent (so CI/release builds stay green).
- **Run:** `dosbox build/dos/ur.exe` — or DOSBox-X, 86Box, or a real PC with
  VGA. ESC at the title returns to DOS (text mode restored).
- **Headless rig** (same Xvfb pattern as the Genesis): `Xvfb :97 &`, then
  `DISPLAY=:97 SDL_VIDEODRIVER=x11 dosbox build/dos/ur.exe`; find the window
  with `xdotool search --name 'UR.EXE'`, focus, tap keys with
  `keydown`/150 ms/`keyup`, shoot with `import -display :97 -window <id>`.

### Still to do

1. **FujiNet online** — fujinet-lib `msdos` (same Watcom toolchain) would make
   DOS the fifth online platform; needs the `src/net` online loop ported and a
   FujiNet-over-serial/parallel story for the PC.
2. **Adlib/OPL2 FM** as an optional upgrade over the speaker (we already speak
   FM from the Genesis YM2612 work), with speaker fallback.
3. Token glide / dice animation (`plat_animate` is a stub, like the ST's).

# src/c64 — Commodore 64 platform layer (future target)

> **Status: not started.** Placeholder. Begin this port after the Atari build is
> playable (and typically after Apple II). The shared core in
> [`src/common`](../common/CLAUDE.md) should drop in unchanged; only this platform
> layer is new work.

Implements the `plat_*` interface for the Commodore 64.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Networking model:
> [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## Hardware notes

- **CPU:** MOS 6510 @ ~1.02 MHz; 64K RAM with bank-switched ROM/IO.
- **VIC-II:** hardware **sprites** (great for the 14 pieces + cursor), character and
  bitmap modes, smooth hardware scrolling, raster interrupts.
- **SID:** excellent 3-voice synthesizer — the best sound of the three platforms;
  worth nice dice/move/capture/win audio.
- **CIA** chips: joystick/keyboard, timers.
- Generally the most capable of the three for arcade-style presentation.

## FujiNet

FujiNet for the C64 attaches via the **IEC** (serial bus), appearing as a fast
network-connected drive. This is FujiNet's newest of our three platforms — verify
current `fujinet-lib` (c64 target) status when starting. The `N:` device API is the
same model as on Atari/Apple II.

## Build & run (target)

- `cc65 -t c64`, `ld65`, then package as `.prg` or a `.d64` image (e.g. `c1541`).
- Run/test in **VICE** (`x64sc`); network test with FujiNet-PC or real hardware.

## When you start this port

1. Confirm the `plat_*` interface still fits.
2. Lean into VIC-II sprites and SID — this platform can look and sound the best.
3. Reuse the exact same networking protocol and codec — no protocol changes.

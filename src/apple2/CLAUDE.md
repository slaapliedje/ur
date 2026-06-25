# src/apple2 — Apple II platform layer (future target)

> **Status: not started.** Placeholder. The Apple II is the **fourth/last** target —
> begin this port after the Atari, Adam, and C64 ports. The shared core in
> [`src/common`](../common/CLAUDE.md) should drop in unchanged; only this platform
> layer (display, sound, input, net shim) is new work.

Implements the `plat_*` interface for the Apple II family (II+, IIe, IIc, IIgs).

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Networking model:
> [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## Hardware notes

- **CPU:** 6502 @ ~1.02 MHz (slower than the Atari — mind performance).
- **No hardware sprites.** Pieces must be drawn/blitted in software.
- **Graphics:** lo-res, hi-res, and (IIe/IIc) double-hi-res. Hi-res color is quirky
  (NTSC artifact color, 7-pixel byte alignment) — plan the board layout around it.
- **Sound:** 1-bit speaker (toggle-timed); effects require CPU cycle counting.
- **Input:** keyboard; paddles/joystick.

## FujiNet

FujiNet attaches via the **SmartPort** bus on the Apple II. Use `fujinet-lib`
(apple2 / apple2enh target); the `N:` device API is the same as on Atari.

## Build & run (target)

- `cc65 -t apple2` or `-t apple2enh` (enhanced IIe), `ld65`, then package into a
  `.dsk` or `.po` image (e.g. AppleCommander).
- Run/test in **AppleWin**, **MAME**, or **Virtual ][**; network test with FujiNet-PC
  or real hardware.

## When you start this port

1. Confirm the `plat_*` interface still fits (it should — it was designed to).
2. Implement display/input/sound for the Apple II here only.
3. Reuse the exact same networking protocol and codec — no protocol changes.

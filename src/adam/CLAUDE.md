# src/adam — Coleco Adam platform layer (future target)

> **Status: not started.** Placeholder. The Adam is the **second** target — begin
> this port once the Atari build is playable. The shared core in
> [`src/common`](../common/CLAUDE.md) should drop in unchanged; only this platform
> layer is new work.

Implements the `plat_*` interface for the **Coleco Adam**.

> Parent context: [`/CLAUDE.md`](../../CLAUDE.md). Networking model:
> [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## ⚠️ This is a Z80 machine, not 6502

Unlike the other three targets, the Adam runs a **Zilog Z80** CPU. That means a
**different toolchain** (z88dk/SDCC instead of cc65) and **different assembly** for
anything hand-written. The shared C core in `src/common/` is written to be
toolchain-neutral so it still compiles here — but this platform layer is entirely
its own world: Z80 asm, a different video chip, a different sound chip, and a
different FujiNet bus.

## Hardware

- **CPU:** Zilog Z80A @ ~3.58 MHz (faster clock than the 6502 targets, but Z80
  instruction timings differ — don't assume relative performance).
- **RAM:** 64 KB base (expandable; 80 KB common). Memory is bank-switched.
- **Video:** **TMS9928A** VDP (same family as ColecoVision / MSX / SG-1000) —
  tile/character modes plus **hardware sprites** (good for the pieces). Video RAM is
  separate and accessed through VDP ports, not direct CPU memory.
- **Sound:** **SN76489** (TI) 3-voice + noise PSG.
- **OS:** the Adam's **EOS** (Elementary OS) / SmartWriter; the Adam can also run
  **CP/M**, which z88dk can target.
- **Bus:** **AdamNet** — an asynchronous serial peripheral bus (RJ12) shared by the
  keyboard, drives, printer, and FujiNet.

## Toolchain (z88dk)

- Target **`coleco`** with the **`adam` subtype** (enables CP/M disk support and the
  adam library).
- C compiler: **`z88dk-zsdcc`** (z88dk's SDCC build) or **`sccz80`** (z88dk's native
  compiler). Assembler/linker: **`z88dk-z80asm`**. Packaging: **`z88dk-appmake`**
  turns the raw binary into a loadable image.
- Reference: https://github.com/z88dk/z88dk (and its Coleco/Adam platform wiki).

## FujiNet

FujiNet attaches to the Adam via the **AdamNet** bus — and the Adam was the
**second** platform FujiNet ever supported, so it is mature. Use `fujinet-lib`
(adam target, built with z88dk) for the `N:` device; the API and our wire protocol
are the same as on every other platform. Existing FujiNet/Adam network games
(ADAMcala; Connect 4, which cross-plays with the Atari) confirm the path works.
See [`src/net/CLAUDE.md`](../net/CLAUDE.md).

## Build & run (target)

- Compile/link with z88dk for `coleco`/`adam`; package with `z88dk-appmake` into an
  Adam disk (`.dsk`) or digital data pack (`.ddp`) image.
- Run/test in **MAME** (`adam` driver) or **ADAMEm**; network test with FujiNet-PC
  or real FujiNet hardware on a real Adam.

## When you start this port

1. Confirm the `plat_*` interface still fits (it was designed to be CPU-agnostic).
2. Implement display (TMS9928A), sound (SN76489), and input here only, in C + Z80 asm.
3. Reuse the exact same networking protocol and codec — **no protocol changes**.
4. Watch the core for any accidental cc65-isms; everything in `common/` must also
   build under z88dk/SDCC.

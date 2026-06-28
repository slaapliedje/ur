---
name: run-ur
description: Build, launch, screenshot, and drive the Royal Game of Ur retro game (Atari 8-bit + Atari 5200). Use when asked to run, start, build, test, screenshot, or play-test Ur, or to verify an Atari/5200 graphics/sound/gameplay change in the emulator.
---

# Run Ur (Atari 8-bit / Atari 5200)

Ur is a cross-compiled 6502 game — there is **no native binary**. You "run" it by
building a ROM and loading it in the **atari800** emulator, then driving it like a
person (keys + screenshots). The harness that does this is
[`driver.sh`](driver.sh): it launches atari800, sends a key sequence, screenshots,
and exits in **one shot** — the agent path. Paths below are relative to the repo
root (this directory's `<repo>/`).

## Prerequisites

This dev host is **Garuda/Arch** (pacman); all tools below were already installed.
Build toolchain (`cl65` = cc65, `zcc` = z88dk; the repo `Dockerfile` is the canonical
build image — z88dk base + cc65 from source) and the drive tools (an **X display** is
required — `DISPLAY=:0`):
```bash
# Arch (this machine) — package names verified via `pacman -Qo`:
sudo pacman -S --needed cc65 z88dk atari800 xdotool imagemagick perl
# Debian/Ubuntu equivalent:
#   sudo apt-get install -y cc65 z88dk atari800 xdotool imagemagick perl
```
cc65 = 6502 targets (Atari / 5200 / C64 / Apple II); z88dk = Z80 (Adam / SMS / GB).

## Build

```bash
make atari      # -> build/atari/ur.xex   (Atari 8-bit, the primary target)
make a5200      # -> build/a5200/ur.a52   (Atari 5200 cartridge, 32K)
make test       # host unit tests for src/common  (-> "20102/20102 checks passed / OK")
```
Other targets build too (`make adam c64 apple2 sms coleco`) but are **not** driven by
this skill — see *Other targets* below. `make` alone builds everything.

## Run — agent path (driver.sh)

Build first, then drive. Each call launches → sends keys → writes a PNG → kills the
emulator:

```bash
# Title screen (boot + screenshot, no input):
.claude/skills/run-ur/driver.sh atari /tmp/ur_title.png

# In-game board (keys: 2 = vs-computer, 1 = roll/ack), then screenshot:
.claude/skills/run-ur/driver.sh atari /tmp/ur_board.png 2 1

# Atari 5200 title (slower boot is handled):
.claude/skills/run-ur/driver.sh a5200 /tmp/ur5200_title.png

# With sound (capture the PipeWire monitor separately if you need audio):
SDL_AUDIODRIVER= .claude/skills/run-ur/driver.sh atari /tmp/ur_snd.png
```
`driver.sh <atari|a5200> <out.png> [key ...]` — keys are `xdotool` keysyms sent in
order. **Always open the PNG and look at it** (blank teal = booted too early; see
Troubleshooting).

**Controls** (what to send): menu `1`–`7` (A8) / `1`,`2`,`4` (5200); in-game press
any key to roll, then `1`–`N` to pick a move; `F4` = OPTION (toggles the music).
On the 5200 the controller keypad maps to the **host number keys**.

## Run — human path

```bash
atari800 -windowed -nobasic -nojoystick build/atari/ur.xex          # A8, interactive
atari800 -5200 -5200-rev altirra -windowed -nojoystick -cart-type 4 -cart build/a5200/ur.a52
```
Inside a Claude Code session, launch the human path with the **`!` prefix** (e.g.
`! atari800 -windowed -nobasic -nojoystick build/atari/ur.xex`) so the GUI runs in
your terminal — a backgrounded atari800 spawned from the agent's shell gets killed
(see Gotchas).

## Other targets (build-verified; different emulators)

- **Coleco Adam** `make adam` → `build/adam/ur.ddp` — run in `mame adam`.
- **ColecoVision** `make coleco` → `build/coleco/ur.rom` — `mame coleco -cart …`.
- **C64** `make c64` → `build/c64/ur.prg` — run in VICE (`x64sc`).
- **Apple II** `make apple2` → `build/apple2/ur.po` — `mame apple2ee` (ProDOS disk).
- **SMS** `make sms` → `build/sms/ur.sms` — renders + playable; run in `mame sms`
  (D-pad = arrows, button 1 = Left Ctrl). Drive like the Atari path but `xset r off`
  first (X key-repeat → extra control-pad edges). See [`src/sms/CLAUDE.md`](../../../src/sms/CLAUDE.md).

## Gotchas (this environment — non-obvious)

- **`sleep` is killed** (command exits 144) in this sandbox. The driver uses
  `perl -e 'select(...)'` for delays instead. Don't add `sleep` to driver flows.
- **Detached GUI procs get SIGSTKFLT (144).** atari800 left running in the
  background dies, so the driver does launch→drive→capture→kill in **one** command.
  Don't try to keep an emulator alive across separate tool calls.
- **`-nojoystick` is essential.** Otherwise atari800 grabs the host keyboard's
  media keys as "joystick 0" and the **keyboard menu stops responding** to xdotool.
- **Send keys with real XTEST**: `xdotool windowactivate --sync $W` then
  `keydown`/`keyup`. `xdotool --window … key` (synthetic) is ignored by atari800's
  SDL input.
- **The 5200 boots slower (~4.5 s)** than the A8 (~2.6 s); capturing too early gives
  a blank teal frame. The driver waits per-target.
- **5200 needs no external BIOS**: `-5200-rev altirra` uses the built-in AltirraOS;
  `-cart-type 4` = standard 32K 5200 cart.
- **Don't use atari800 `-aname`** (audio recording) — it *silences* live audio. For
  sound, use `-sound` and record the PipeWire sink monitor with `parecord`.
- **Keep each driver call short** (≈ under 6 s total). Long key sequences can be
  killed mid-run by the sandbox; split into multiple short calls if needed (note
  the emulator state resets between calls — it's relaunched each time).

## Troubleshooting

- **Blank/teal screenshot** → boot wait too short (esp. 5200) or window captured
  before it mapped. Raise `BOOT` in `driver.sh` for that target.
- **`ERROR: no atari800 window`** → launch failed (the driver prints the atari800
  log) or the window hadn't mapped — usually a missing build artifact (`make atari`
  first) or X not reachable (`DISPLAY`).
- **Keys do nothing** → missing `-nojoystick`, or the window wasn't focused
  (`windowactivate --sync`). The driver handles both.
- **`make a5200` "RODATA overflows ROM"** → the 5200 needs a 32K cart
  (`__CARTSIZE__=0x8000`, already set in `makefiles/a5200.mk`).

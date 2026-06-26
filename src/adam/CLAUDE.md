# src/adam — Coleco Adam platform layer (future target)

> **Status: playable — local + online, colour + sound.** `src/adam/main.c` reuses
> the shared core and renders a **colour** board: lapis background, gold
> rosettes/title, pieces as **hardware-sprite tokens** (cream-with-dark-pip /
> dark-with-cream-pip) over conio lanes/labels, with **SN76489 sound effects**.
> Menu: hot-seat, vs-AI, **online (FujiNet)**, and Set name/server. `make adam` →
> `build/adam/ur.ddp` (run in MAME `adam` / ADAMEm). The shared core + protocol
> codec drop in unchanged; only this layer is new.

## Sound (SN76489)

`src/adam/sound.c` drives the TI SN76489 PSG directly via I/O **port 0xFF**
(`outp()` from `<stdlib.h>`) — simpler than the VGM-based PSGlib for short blips
and needs no per-frame tick. Tone writes are `0x80|ch<<5|(n&0xF)`, `(n>>4)&0x3F`,
`0x90|ch<<5|atten`; noise is `0xE0|ctrl` + `0xF0|atten`; period `n = 223722/Hz`
(the z88dk coleco `psgT()` value), precomputed to avoid a runtime 32-bit divide.
`sfx_roll/move/capture/rosette/score/win` are short blocking sequences fired from
the same events as the Atari (`sfx_for_result()` after a move; flags-based in the
online loop). Verified by recording MAME audio (`-wavwrite`) and confirming tone
energy during a boot-time sound test. NOTE: the build now adds `-Isrc/adam` so
`main.c` finds `sound.h` (and any future Adam header).

## Online cross-play (FujiNet N:TCP)

`online_game()` speaks the **same `src/common/proto` wire protocol** as the Atari,
so the Adam joins the **same server games** and cross-plays with Atari (and future
C64/Apple II) clients. It uses `fujinet-adam` exactly like the Atari uses
`fujinet-atari`: `network_init` / `network_open("N:TCP://host:1234/", RW)` / a
`ur_proto_join` with the player name / then a poll loop — `network_status` until
`UR_STATE_MSG_LEN` bytes are ready, `network_read`, `ur_proto_decode_state` — and
`ur_proto_roll` / `ur_proto_move` for our turns. The blocking EOS keyboard means
there's no mid-wait "press a key to cancel" (unlike the Atari); the server's
~60s AI fallback keeps a lone player from waiting forever.

**Name/host are configurable and persistent** (menu "Set name"/"Set server"),
using the **same FujiNet AppKey blob as the Atari** (creator `0x5552`, app 1,
key 0; layout `name[8]+wins[2]+hostlen[1]+host[]`) so a profile is interchangeable
between builds. `online_game()` (once `network_init` confirms a FujiNet) calls
`profile_load()` then `lobby_host_from_appkey()` (creator 1 / app 1 / key 6 — the
lobby handoff) so a lobby-launched Adam auto-connects to the chosen server.
`edit_field()` does the on-screen text entry via a raw (unfiltered) EOS read so
it sees RETURN/DELETE. **All `fuji_*` calls live in the online/settings paths
only — never at boot** — because, like `network_init`, they block without a
FujiNet (so local play still works under MAME; Set name/server let you type but
the appkey *save* needs real hardware). Defaults: host `thefnords.com`, name
`ADAM`.

**Testing caveat:** MAME's `adam` driver has **no FujiNet emulation**, so
`network_init()` blocks waiting for a device that isn't there — selecting Online
under MAME just sits on the title screen. Build/integration verify fine there;
real end-to-end testing needs a **real Adam + FujiNet** (or an emulator bridged to
FujiNet-PC). The Atari path is the proven reference.

## Colour & sprite pieces (and a visual-iteration tip)

conio supports colour on this target: `textcolor()`, `textbackground()`,
`bordercolor()` take standard CGA-ish indices (`conio.h` `enum colors`) and
z88dk's `conio_map_colour` maps them to TMS9928A inks. The mapping gives us the
Atari build's Standard-of-Ur look: **BLUE → dark blue (lapis)** background,
**YELLOW → light yellow (gold)** for the title/rosettes/roll, **WHITE** for
labels. Per-character colour works (the default console is not the monochrome
text mode), so each board glyph is coloured individually in `draw_board()`.

The **pieces are TMS9928A 16x16 hardware sprites** styled like a real Ur set —
a cream token with a dark pip (Light) and a dark token with a cream pip (Dark) —
via z88dk's `<video/tms99x8.h>` (the same VDP library the conio console is built
on, so the sprite tables/registers it uses are already set up and the two
coexist). TMS sprites are single-colour, so **each piece is two sprites**: a
`g_ring16` donut in the body colour at a LOW sprite id (drawn in front), plus a
`g_pip16` centre dot in the contrast colour at a HIGH id (behind, showing through
the ring's transparent hole). `place_pieces()` lays all rings first then all
pips, so if the 4-sprites-per-scanline limit is ever hit only *pips* drop — the
piece bodies never vanish — and ends with a sprite-list terminator (y `0xD0`).
`vdp_set_sprite_mode(sprite_large)` selects 16x16; handles 0/1 hold ring/pip.
The menu calls `hide_sprites()` (terminator at sprite 0) since `clrscr` clears
the name table but **not** sprites. Colours: cream = `LIGHT_YELLOW`, "brown" =
`DARK_RED` (no true brown in the 16-colour palette; reads reddish).

To keep 16x16 tokens from overlapping (which would breach the TMS9918
4-sprites-per-scanline limit), the board uses a **24px column / 16px row pitch**
(`cellx`/`celly` → 3 char cols, 2 char rows). Each board square is drawn as a
**16x16 colour cell** the same size as a token (`fill_cell` paints a 2x2 block
of coloured spaces — cyan lanes, gold rosettes) so a round token sits *inside* a
matching cell rather than dwarfing tiny text markers. The cut-away corners
(mid rows, side columns) simply aren't drawn, giving the Ur H-shape.

The taller board means a **two-column layout**: board on the left, right-hand
info panel at `INFO_COL` for Turn (player name colour-coded white/green),
Light/Dark home counts, Roll, and the move list. Note `fill_cell` leaves the
text background set to a cell colour, so reset `textbackground(COL_BG)` before
drawing the panel. `-DUR_DEMO` boots into a representative board *and* calls
`choose_move` so the move-list panel renders for a screenshot.

Visual iteration is slow over the harness (data-pack load ~60s, menu input is
finicky), so `main.c` has a compile-time **`-DUR_DEMO`**: boot straight into a
representative coloured board (no menu/input) for a quick boot→screenshot loop.
Build it with the raw `zcc` line `make adam` prints, adding `-DUR_DEMO=1`. Run
under MAME with `-nothrottle` to cut the load wait. Plain `make adam` is the real
game.

## Keyboard input (important gotcha)

z88dk's `coleco` target has **no keyboard**: `libsrc/target/coleco/stdio/getk.asm`
is a stub that returns 0 ("the Colecovision doesn't have a keyboard"), so conio's
`cgetc()`/`kbhit()` never see a key. The Adam keyboard is reached through **EOS**
on the AdamNet bus, via tschak909's **eoslib** (already linked for `eos.lib`):

- `eos_read_keyboard()` — **blocks** while the type-ahead buffer is empty and
  returns the next key otherwise. In **MAME** it is *level-triggered*: it keeps
  returning the same code for as long as the key is physically held, and the game
  loop runs far faster than a keypress — so a single press will satisfy several
  reads and blow through prompts unless debounced. (`main.c` adds a short busy-wait
  `settle()` after each read; real Adam hardware queues one code per press, so the
  settle is effectively a no-op there.)
- `eos_keyboard_status()` returns a constant `0x80` in MAME — **not** a usable
  key-pending flag.
- `eos_start_read_keyboard()` / `eos_end_read_keyboard()` are a *non-blocking*
  pair (end returns the key, or `13` = NAK and auto-reissues) but race on AdamNet
  read latency, so we don't use them for input.

All verified empirically by driving MAME under X11 (xdotool + ImageMagick
`import`) and reading the screen back.

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

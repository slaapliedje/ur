# src/c64 — Commodore 64 platform layer (3rd target)

> **Status: local play + SID sound + the VIC-II sprite colour showcase + FujiNet
> online.** Build variants of `src/c64/main.c`, picked with make flags:
>
> - **`make c64` (default) — multicolor SPRITE tokens.** The traditional
>   **horizontal 3×8 board** (Light row on top, shared lane in the middle, Dark row
>   on the bottom; rosettes + lane dots drawn with the custom charset). Pieces are
>   **VIC-II multicolor sprites** — a multicolor sprite has 4 colours, so the tokens
>   are genuinely **two-tone**: a **bone body with a brown pip** (Light) and a
>   **brown body with a bone pip** (Dark), exactly like a real Ur set. The C64 has
>   only 8 hardware sprites but the board holds up to 14 pieces, so a **raster-
>   interrupt sprite multiplexer** ([`mux.s`](mux.s)) reuses the 8 sprites across
>   the 3 rows (8 × 3 = 24 token slots; a single row never holds more than 8
>   pieces). This is the C64 colour showcase — see
>   [`docs/future-enhancements.md`](../../docs/future-enhancements.md).
> - **`make c64 CHARSET=1` — charset fallback (no sprites, no raster IRQ).** The
>   earlier vertical board drawn entirely with a custom charset: round disc tokens,
>   shaped (8-point) rosettes, lane dots, coloured from colour RAM (white Light /
>   brown Dark). The known-good baseline; handy if the multiplexer ever misbehaves.
> - **`make c64 ONLINE=1` — FujiNet online.** Adds `N:TCP` server-authoritative play
>   (same wire protocol + server as the Atari/Adam): the lobby/profile menu (set
>   name, set server host, leaderboard), the AppKey profile, and lobby host pickup.
>   Combine with `CHARSET=1` for the vertical board. See **FujiNet** below.
>
> All reuse the shared core, the menu/turn loop, and **SID sound** (`src/c64/sound.c`).
> `cgetc`/`kbhit` work natively. `make c64` → `build/c64/ur.prg` (run in VICE
> `x64sc`; online needs FujiNet). Builds clean under cc65 2.18.

## Sprite multiplexer (`mux.s`)

The board's three rows are placed ~56 raster lines apart, far more than a sprite's
21 lines, so a chain of raster interrupts can reprogram the same 8 hardware sprites
per row:

- **Band 0** (top/Light row) is the frame-top IRQ: it programs the row's tokens
  **and chains to the KERNAL IRQ (`$EA31`)** so keyboard scan / jiffy clock keep
  running (`cgetc` still works). Bands 1 (shared) and 2 (Dark) just reprogram the
  sprites and restore+RTI via `$EA81`.
- C fills per-band tables (`band_x/ptr/col/en/y/trig`, exported from `mux.s`) each
  redraw; the IRQ just blits the active band's table into the VIC registers
  ($D000.. positions, `$07F8` pointers, `$D027..` colours, `$D015` enable).
- Sprite **shapes** live in the cassette buffer (`$0340`/`$0380`, blocks 13/14 —
  free and 64-byte aligned, no linker-config fuss). Multicolor: MC1 (`$D025`)=brown,
  MC2 (`$D026`)=bone, shared; each sprite's individual colour is the body, and the
  pip uses MC1 (Light) or MC2 (Dark) — so one disc shape gives both inverted tokens.
- `mux_install()` on entering a game, `mux_stop()` on leaving (restores the normal
  KERNAL IRQ + CIA timer and disables sprites).

> **Verified in VICE** (all 3 rows lit at once, multiple tokens per row, no flicker).
> Raster multiplexers can be timing-sensitive on real hardware, but the 56-line band
> separation gives wide margins — still, confirm on a real C64.

## Sound (SID)

`src/c64/sound.c` drives the SID at `$D400` (voice 1) directly: set frequency
(`Fn = Hz / 0.0596` PAL, precomputed), a punchy ADSR, then waveform|gate; busy-wait;
gate off. `sfx_roll/move/capture/rosette/score/win` + `sfx_for_result()` fire on
the same events as the other builds (triangle/sawtooth tones, noise for the dice
rattle). Verified by recording VICE audio (`-soundrecdev wav`) and confirming tone
energy. `-DUR_SNDTEST` plays the whole set on boot for capture.

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

## FujiNet (online — `make c64 ONLINE=1`)

FujiNet for the C64 attaches via the **IEC** (serial bus). The `N:` device API and
the Ur wire protocol are identical to the Atari/Adam, so `online_game()` is a direct
port of the Atari's: `network_init` → `network_open(N:TCP://host:1234/, RW)` → send
`ur_proto_join(name)`, then the server-authoritative loop — render each STATE
snapshot, send ROLL/MOVE intents, poll with `read_state()`. Plus the AppKey **profile**
(creator `0x5552`='UR', name/wins/host) shared with the Atari/Adam, **lobby host
pickup** (creator `0x0001`, appkey `6`), and a `/top` **leaderboard** over `N:HTTP`.

- Built with `make c64 ONLINE=1`: `c64.mk` downloads `fujinet-c64-<ver>.lib` and adds
  `-DUR_ONLINE`. Frame timing uses the KERNAL jiffy ($A2), which keeps ticking under
  the sprite multiplexer (band 0 chains to the KERNAL IRQ).
- **Memory:** fujinet-lib (~8K) makes the program fill VIC bank 0, leaving no room for
  the 2K custom charset at `$3800` (the screen must stay at `$0400` for conio). So
  **online builds use the ROM charset** — board cells become reverse-space **colour
  tiles** (gold rosette / grey lane) instead of the custom star/dot glyphs; the
  multicolor **sprite tokens are unchanged**. Local-only builds keep the custom
  charset. (A future option: a custom linker config + relocated screen to keep both.)
- **cc65 2.18 shim:** the prebuilt lib needs `c_sp`/`__bzero`/`__oserror` symbol
  aliases on an old cc65; `common.mk` auto-enables `src/atari/csp_compat.s` (plain
  6502, shared with the Atari) via `CSP_COMPAT`.
- **Tested:** builds, boots to the lobby menu, local play renders (tiles + sprites),
  and the Online option fails the connection **gracefully** in VICE (no FujiNet:
  `network_open` → "Connect failed"). Full cross-play needs **FujiNet(-PC) + the Ur
  server**, same as the Atari. The `CHARSET=1 ONLINE=1` build runs online with no
  raster IRQ — the safest option if the sprite build's IEC/raster timing ever fights.

## Build & run (target)

- `cc65 -t c64`, `ld65`, then package as `.prg` or a `.d64` image (e.g. `c1541`).
- Run/test in **VICE** (`x64sc`); network test with FujiNet-PC or real hardware.

## When you start this port

1. Confirm the `plat_*` interface still fits.
2. Lean into VIC-II sprites and SID — this platform can look and sound the best.
3. Reuse the exact same networking protocol and codec — no protocol changes.

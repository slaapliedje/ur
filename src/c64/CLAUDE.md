# src/c64 — Commodore 64 platform layer (3rd target)

> **Status: the dense "Standard of Ur" board (SMS-style) + SID sound + FujiNet
> online.** Build variants of `src/c64/main.c`, picked with make flags:
>
> - **`make c64` (default, local) — DENSE multicolor-charset mosaic.** Matches the
>   SMS showpiece: the **horizontal 3×8 H-board** drawn as **chunky 2×2 carved
>   cells** — gold **rosette stars** at the 5 rosette squares, bullseye **eyes** down
>   the shared lane, five-dot **quincunx** studs on the private lanes — with round
>   **two-tone tokens** (shell-white Light, carnelian-red Dark) and shell/red tray
>   beads, in the Standard-of-Ur palette (lapis field, light-blue cell body, black
>   shadow, gold/white/red inlay). Everything is the **custom multicolor charset**
>   (`$3800`); **no sprites/raster IRQ** (the dense rows are adjacent, which is
>   incompatible with the sprite multiplexer — see below). Glyphs (codes `0xC4..0xD3`)
>   are **precomputed** (`dense_chars[]`, regenerate with the host tool
>   [`tools/c64-dense-glyphs.c`](../../tools/c64-dense-glyphs.c)) and memcpy'd in,
>   keeping the binary clear of the charset RAM.
> - **`make c64 ONLINE=1` — FujiNet online + the sprite-token board.** The earlier
>   **VIC-II multicolor SPRITE tokens** (two-tone bone/brown) via the **raster-
>   interrupt multiplexer** ([`mux.s`](mux.s)) on a ROM-charset board (fujinet-lib
>   fills VIC bank 0, so no custom charset). Adds `N:TCP` server-authoritative play
>   (same wire protocol + server as the Atari/Adam): the lobby/profile menu, the
>   AppKey profile, lobby host pickup. See **FujiNet** below.
> - **`make c64 CHARSET=1` — charset fallback (vertical board).** The earlier
>   vertical layout drawn with a custom charset; the known-good baseline.
>
> All reuse the shared core, the menu/turn loop, and **SID sound** (`src/c64/sound.c`).
> `cgetc`/`kbhit` work natively. `make c64` → `build/c64/ur.prg` (run in VICE
> `x64sc`; online needs FujiNet). Builds clean under cc65 2.18.
>
> **Why the local board drops the sprite multiplexer:** the SMS-dense look needs the
> three rows adjacent (2×2 cells), but the 8-sprite multiplexer needs them ~56 raster
> lines apart (a sprite is 21 px tall, 8 reused per row) — the two are incompatible.
> The user chose the dense mosaic for the local showcase; the multiplexer lives on in
> the online build. **Watch the binary size:** the program loads at `$0801` and grows
> up toward the charset at `$3800` — keep it under `$3800` (the dense build ends
> ~`$3549`) or `install_dense_glyphs`/`setup_charset` will overwrite the program.

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

> **Shipped as its own download.** `make c64-online` (= `make c64 ONLINE=1`) emits
> **`build/c64/ur-online.prg`** — a distinct name so it coexists with the local
> `ur.prg`. `make release` builds both, and the itch **c64** channel carries both
> (the local default keeps the custom-charset showcase; `-online` adds FujiNet). So
> the C64 now ships a FujiNet-capable binary alongside the local one.

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

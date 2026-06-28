# Ur

A video-game implementation of the **Royal Game of Ur** — the ~4,600-year-old
Mesopotamian board game — for retro 8-bit home computers, with cross-platform
network multiplayer over [FujiNet](https://fujinet.online/).

**Primary targets (in build order):** Atari 8-bit (400/800/XL/XE) first, then
Coleco Adam, Commodore 64, and Apple II. This deliberately inverts the usual
historical priority (games once led with the Apple II and C64, often skipping the
Atari and Adam). The 6502 machines (Atari, C64, Apple II) are built with cc65; the
Z80-based Adam is built with z88dk — the shared game core compiles for both.

**Also ported** (local play, no FujiNet on these machines): a **ColecoVision**
cartridge, the **Sega Master System** + **Game Gear**, a dual-mode **Game Boy /
Game Boy Color** cart, the **Atari 5200**, and the **NES / Famicom**.

## Status

**Playable on every target.** All eleven builds run locally (hot-seat + vs-AI) with
a unified horizontal *Standard of Ur* board — carved cells, gold rosettes, two-tone
tokens — and chip sound + the Hurrian Hymn title theme (a couple of the bonus ports
still need audio; see [`ROADMAP.md`](ROADMAP.md)). **FujiNet online** (`N:TCP`,
server-authoritative, cross-platform) is implemented on the four FujiNet platforms
(Atari / Adam / C64 / Apple II); end-to-end cross-play needs FujiNet hardware (or
FujiNet-PC) plus the game server. See [`ROADMAP.md`](ROADMAP.md) for the per-phase
status.

## How it's built

- A shared, portable **C core** (game rules, AI, networking protocol) compiled for
  every platform — with [cc65](https://cc65.github.io/) on the 6502 machines
  (Atari, 5200, C64, Apple II, NES) and [z88dk](https://github.com/z88dk/z88dk) on
  the Z80-family machines (Adam/ColecoVision, SMS, Game Gear, Game Boy).
- **Thin per-platform layers** (in C + assembly) for graphics, sound, and input.
- **Multiplayer** via the FujiNet Game System: a server-authoritative game server
  mediates turns and is discoverable through the FujiNet Lobby.

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — project overview, architecture, and toolchain.
- [`docs/rules.md`](docs/rules.md) — the rules of the game.
- [`docs/protocol.md`](docs/protocol.md) — the multiplayer wire protocol.
- Each `src/*/CLAUDE.md` — guidance for that subsystem.

## License

[GNU General Public License v3.0](LICENSE) (GPLv3). New source files should carry
an `SPDX-License-Identifier: GPL-3.0-or-later` header. See [CONTRIBUTING.md](CONTRIBUTING.md).

# Ur

A video-game implementation of the **Royal Game of Ur** — the ~4,600-year-old
Mesopotamian board game — for retro 8-bit home computers, with cross-platform
network multiplayer over [FujiNet](https://fujinet.online/).

**Target platforms (in build order):** Atari 8-bit (400/800/XL/XE) first, then
Coleco Adam, Commodore 64, and Apple II. This deliberately inverts the usual
historical priority (games once led with the Apple II and C64, often skipping the
Atari and Adam). The 6502 machines (Atari, C64, Apple II) are built with cc65; the
Z80-based Adam is built with z88dk — the shared game core compiles for both.

## Status

Early scaffolding. No game code yet — the repository currently holds project
documentation and the directory skeleton.

## How it's built

- A shared, portable **C core** (game rules, AI, networking protocol) compiled for
  every platform with the [cc65](https://cc65.github.io/) toolchain.
- **Thin per-platform layers** (in C + 6502 assembly) for graphics, sound, and input.
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

# Ur — the Royal Game of Ur for retro 8-bit computers

This repository is a video-game implementation of the **Royal Game of Ur**, an
ancient Mesopotamian board game (~2600 BCE, excavated at the city of Ur), built
for retro 8-bit home computers with **cross-platform network multiplayer over
FujiNet**.

> This is the master context file. Each subsystem has its own `CLAUDE.md` with
> more specific guidance — see [Repository layout](#repository-layout).

## Target platforms (build order)

1. **Atari 8-bit** (400/800/XL/XE) — the primary, first target. *(6502)*
2. **Coleco Adam** — second target. *(**Z80**, not 6502 — different toolchain; see below)*
3. **Commodore 64** — third target. *(6502 / 6510)*
4. **Apple II** — fourth target. *(6502)*

…plus a **ColecoVision** build (`make coleco`) that falls out of the Adam layer for
free — same Z80 / TMS9928A / SN76489 core, controller (keypad + FIRE) input, no
FujiNet. See [`src/adam/CLAUDE.md`](src/adam/CLAUDE.md).

All four targets now have a working platform layer with local play **and FujiNet
online**: **Atari** (carved board + PMG two-tone tokens + DLI sheen + POKEY),
**Adam** (carved Graphics-II board + sprite tokens + SN76489), **C64** (carved
multicolor board + two-tone sprite-token showcase + SID), and
**Apple II** (lo-res *and* double-hi-res 16-colour boards + 1-bit speaker). Every
target speaks the same `N:TCP` wire protocol to the Go server. The Atari and Adam
online paths are the most exercised end-to-end; the C64/Apple II online builds are
verified to boot + fail-gracefully without FujiNet (full cross-play needs FujiNet +
the server). See each `src/<platform>/CLAUDE.md` and [`ROADMAP.md`](ROADMAP.md).

This order deliberately inverts the usual historical priority: back in the day,
games tended to target the Apple II and C64 first — and often skipped the Atari and
Adam entirely. We lead with the frequently-overlooked Atari and Adam. It is also
practical: FujiNet/Adam is very mature and Adam↔Atari cross-play is already proven,
making the Adam a strong early multiplayer companion to the Atari.

> **CPU split:** three of the four targets are 6502-family; the **Coleco Adam is a
> Zilog Z80** (our second target). This is the main reason the toolchain is not
> uniform — see [Toolchain](#toolchain). The shared C core is designed to span both.

## Architecture

A **hybrid cc65** design: maximize shared code, drop to assembly only where it
pays off.

- **Portable C core** (`src/common/`) — game rules, AI, turn state machine, and
  the network-protocol codec. Written as **toolchain-neutral standard C** so it
  builds under both `cc65` (the 6502 machines) and `z88dk`/SDCC (the Z80 Adam).
- **Thin per-platform layers** (`src/atari/`, `src/apple2/`, `src/c64/`,
  `src/adam/`) — display, sound, input, and timing. C plus assembly for hardware/
  hot paths: `ca65` (and optionally MADS) on the 6502 targets, Z80 asm
  (`z88dk-z80asm`) on the Adam.
- **Networking** (`src/net/`) — client-side networking through FujiNet's `N:`
  device via `fujinet-lib`, plus FujiNet Game System (FGS) lobby/game-client glue.
- **Game server** (`server/`, future) — a modern, server-authoritative game
  server that mediates multiplayer turns and registers with the FGS Lobby.

**Boundary rule:** `src/common/` must never include platform headers or touch
hardware. Platform layers implement an abstract interface (`plat_draw_*`,
`plat_input_*`, `plat_sound_*`, `plat_net_*`) that the core calls — the core
calls the platform, never the reverse. Keep `#ifdef PLATFORM` out of `common/`.

## Toolchain

- **6502 targets (Atari, Apple II, C64) — [cc65](https://cc65.github.io/)** suite:
  `cc65` (C compiler), `ca65` (assembler), `ld65` (linker), `ar65`. Targets
  `-t atari`/`-t atarixl`, `-t apple2`/`-t apple2enh`, `-t c64`.
  Users guide: https://cc65.github.io/doc/cc65.html
- **Z80 target (Coleco Adam) — [z88dk](https://github.com/z88dk/z88dk)**: C compilers
  `z88dk-zsdcc` (its SDCC build) and `sccz80`, plus `z88dk-z80asm` (assembler) and
  `z88dk-appmake` (packages binaries into machine formats). Target is `coleco` with
  the **`adam` subtype** (includes CP/M disk support and the adam library).
- **[fujinet-lib](https://github.com/FujiNetWIFI/fujinet-lib)** — C library wrapping
  the FujiNet `N:` device (open/read/write/close/status). It is **multi-toolchain**
  and supports all four of our targets — atari, apple2, c64, **adam** (plus coco,
  dos, rc2014, pmd85) — built per-platform (cc65 for the 6502 trio, z88dk for the
  Adam). So the same `N:` API and the same wire protocol are available everywhere.
- **[fujinet-build-tools](https://github.com/FujiNetWIFI/fujinet-build-tools)** —
  project/build template for cc65 + fujinet-lib.
- **MADS** (Mad Assembler) — optional, for hand-tuned Atari-only assembly modules.

### Emulators / test rigs

- **Atari:** [Altirra](https://www.virtualdub.org/altirra.html)
- **Apple II:** AppleWin / MAME / Virtual ][
- **C64:** [VICE](https://vice-emu.sourceforge.io/)
- **Coleco Adam:** MAME (`adam` driver) / ADAMEm
- **Networking without hardware:**
  [FujiNet-PC](https://github.com/FujiNetWIFI/fujinet-pc) (emulated FujiNet)

> `cc65`, `z88dk`, and the emulators are developer prerequisites (not assumed
> installed). Full setup, the Docker dev image, and the build/run/test loop are in
> [`docs/development.md`](docs/development.md).

## The game (Finkel ruleset, summary)

We follow the **Finkel ruleset** (Irving Finkel, British Museum), the de-facto
standard for modern play.

- **Board:** 20 squares in 3 rows of 8 with 4 squares cut away — a 4×3 block joined
  to a 2×3 block by a 2-square bridge. Each player has a private entry section and
  a private exit section; the middle row is shared (the combat zone).
- **Pieces:** 7 per player; each travels a 14-square path on and then off the board.
- **Dice:** 4 binary tetrahedral dice (2 marked corners each). Roll = number of
  marked corners up = **0–4**, distributed **1 / 4 / 6 / 4 / 1** out of 16.
- **Rosettes:** 5 special squares — landing on one grants an **extra roll** and the
  piece is **safe from capture**.
- **Capture:** landing on an opponent's piece in the shared middle row sends it
  back to start (rosette squares are safe).
- **Bear-off:** a piece must roll the **exact** count to leave the board.
- **Win:** first player to bear off all 7 pieces wins.

Full rules, the per-player path table, and exact rosette positions live in
[`docs/rules.md`](docs/rules.md). **Those exact square indices must be verified
against authoritative sources before being encoded** in `src/common/`.

## Multiplayer over FujiNet

Cross-platform play works because two things are platform-agnostic: FujiNet's
**`N:` network device** (same open/read/write/close/status model on every machine,
even though the physical bus differs), and **one documented wire protocol** that
every client speaks identically.

We use the **FujiNet Game System (FGS)**: a server-authoritative game server
mediates turns and registers with the central Lobby at `fujinet.online`; a
per-platform lobby client discovers games and launches our game client. This is
the same model proven by FujiNet's 5 Card Stud (already running on 6+ platforms).

Details and the wire protocol: [`src/net/CLAUDE.md`](src/net/CLAUDE.md) and
[`docs/protocol.md`](docs/protocol.md).

## Repository layout

```
CLAUDE.md            ← you are here (master context)
README.md            ← short human-facing intro
LICENSE              ← GPLv3
CONTRIBUTING.md      ← contributor guide
ROADMAP.md           ← phased plan (scaffold → networked release)
Makefile             ← top-level build (drives cc65 + z88dk)
makefiles/           ← per-platform build fragments + host-test
Dockerfile           ← dev image: cc65 + z88dk + host gcc
.devcontainer/       ← VS Code dev container (uses Dockerfile)
.editorconfig
.github/             ← CI workflow (build.yml) + issue/PR templates
docs/
  development.md     ← tools, setup, build/run/test loop
  rules.md           ← full Finkel ruleset + path/rosette tables
  protocol.md        ← Ur wire-protocol spec (the cross-platform contract)
  design.md          ← game design (modes, UX, AI, sound)
  visual-design.md   ← per-platform plan to make each port look great (HW strengths)
  architecture.md    ← layering, data model, per-platform memory maps
src/
  common/            ← portable C core (rules, AI, state machine, protocol codec)
    CLAUDE.md
    plat.h           ← the platform interface contract (core ↔ platform layers)
  net/CLAUDE.md      ← networking (N: device / fujinet-lib) + FGS lobby/game client
  atari/CLAUDE.md    ← Atari 8-bit platform layer (1st target, 6502)
  adam/CLAUDE.md     ← Coleco Adam platform layer (2nd target, Z80 / z88dk)
  c64/CLAUDE.md      ← Commodore 64 platform layer (3rd target, 6502)
  apple2/CLAUDE.md   ← Apple II platform layer (4th target, 6502)
tests/               ← host unit tests for src/common
server/              ← modern game server (future); see src/net/CLAUDE.md
lib/                 ← fetched deps (fujinet-lib); git-ignored
build/               ← build output; git-ignored
```

**New here?** Read this file, then [`docs/development.md`](docs/development.md) to set
up tools, [`ROADMAP.md`](ROADMAP.md) for what's next, and
[`docs/architecture.md`](docs/architecture.md) for the technical design.

## Coding conventions

- **Tiny RAM budget** (~48–64K total, often less usable). Prefer fixed-size,
  statically-allocated data structures; avoid heap-heavy patterns. Zero page is
  scarce and precious — reserve it for hot variables.
- Use `<stdint.h>` sized types (`uint8_t`, `int8_t`, `uint16_t`); both the 6502 and
  the Z80 are 8-bit and **little-endian**. Favor `uint8_t` and avoid needless 16-bit
  math. Keep `src/common/` **toolchain-neutral** — no cc65- or z88dk-specific
  extensions — so the same source compiles for both CPU families.
- Keep the game core **deterministic** — the server and clients must agree on
  state from the same inputs.
- Platform-specific code stays in its platform directory; `common/` stays pure.
- Match the surrounding file's style when editing; keep comments purposeful.

## Reference projects

- FGS servers + Lobby (Go reference) — https://github.com/FujiNetWIFI/servers
  (`/lobby`, `/5cardstud`)
- FGS clients (lobby + game clients) — https://github.com/FujiNetWIFI/fujinet-apps
  (`/lobby`, `/5cardstud`)
- Cross-platform app layout reference — https://github.com/FujiNetWIFI/fujinet-cater
- z88dk (Z80 toolchain for the Adam) — https://github.com/z88dk/z88dk
- FujiNet for Coleco Adam — https://fujinet.online/coleco-adam/ ·
  AdamNet notes https://github.com/FujiNetWIFI/fujinet-config/blob/main/notes/all-about-adamnet.md
- FujiNet project hub — https://fujinet.online/ ·
  docs https://fujinetwifi.github.io/fujinet-docs/
- Royal Game of Ur rules — https://royalur.net/rules ·
  https://en.wikipedia.org/wiki/Royal_Game_of_Ur

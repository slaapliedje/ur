# Development guide

How to set up tools, build each platform, and run/test the game. See
[`/CLAUDE.md`](../CLAUDE.md) for the architecture this guide builds.

> **Status:** the toolchain/build/CI scaffolding exists, but there is **no game
> code yet**, so the build targets currently do nothing useful. The first real
> milestone is a "hello, FujiNet" Atari build (see [`ROADMAP.md`](../ROADMAP.md)).

## The two-toolchain reality

Three targets are 6502 and one is Z80, so there are **two C toolchains**:

| Toolchain | CPU | Targets | Notes |
|-----------|-----|---------|-------|
| [cc65](https://cc65.github.io/) | 6502 | Atari, C64, Apple II | `cl65` drives compile+link |
| [z88dk](https://github.com/z88dk/z88dk) | Z80 | Coleco Adam | `zcc` (zsdcc/sccz80) + `z88dk-appmake` |

The shared `src/common/` C core must compile under **both** — keep it
toolchain-neutral (see [`src/common/CLAUDE.md`](../src/common/CLAUDE.md)).

## Setup: toolchains via Docker (recommended)

The toolchains live in one reproducible image (the same one CI uses), built from
the repo [`Dockerfile`](../Dockerfile). Emulators are installed **natively** (next
section) because they're interactive GUI apps.

```sh
# Build the dev image once (cc65 + z88dk + host gcc)
docker build -t ur-dev .

# Run any build/test command inside it (your working copy is bind-mounted at /src)
docker run --rm -it -v "$PWD":/src ur-dev make test
docker run --rm -it -v "$PWD":/src ur-dev make atari
docker run --rm -it -v "$PWD":/src ur-dev          # interactive shell
```

**VS Code:** open the folder and "Reopen in Container" — [`.devcontainer/`](../.devcontainer/devcontainer.json)
uses the same `Dockerfile`.

> First-build validation: confirm `cl65 --version`, `zcc`, and `cc65 --version`
> all run inside the container. If the z88dk base image is ever not apt-based,
> switch the `Dockerfile` to the multi-stage approach noted in its comments.

### Native toolchain install (alternative)

- **cc65** — package managers (`apt install cc65`, `brew install cc65`) or build
  from https://github.com/cc65/cc65.
- **z88dk** — https://github.com/z88dk/z88dk (nightly builds recommended; the
  `coleco`/`adam` target needs a current build). Set `ZCCCFG`/`PATH` per its docs.

## Setup: emulators (install natively)

| Platform | Emulator | Source |
|----------|----------|--------|
| Atari 8-bit | **Altirra** | https://www.virtualdub.org/altirra.html |
| Coleco Adam | **MAME** (`adam` driver) / **ADAMEm** | https://www.mamedev.org/ |
| Commodore 64 | **VICE** (`x64sc`) | https://vice-emu.sourceforge.io/ |
| Apple II | **AppleWin** / **MAME** / Virtual ][ | https://github.com/AppleWin/AppleWin |
| Networking (all) | **FujiNet-PC** | https://github.com/FujiNetWIFI/fujinet-pc |

**FujiNet-PC** emulates the FujiNet device so you can test networking with no
hardware. Point it (and the game's `N:` connections) at a locally-running game
server during development.

## Dependencies: fujinet-lib

[`fujinet-lib`](https://github.com/FujiNetWIFI/fujinet-lib) provides the `N:`
device API for every target (built with cc65 for 6502, z88dk for the Adam). The
build fetches/vendors it via `make deps` (see [`Makefile`](../Makefile)); it is
git-ignored under `lib/`.

## Build / run / test loop

```sh
make deps          # fetch fujinet-lib for all targets (one-time / on update)
make test          # build & run the host unit tests for src/common (uses host gcc)
make atari         # -> build/atari/ur.xex / ur.atr   (cc65)
make adam          # -> build/adam/ur.dsk             (z88dk)
make c64           # -> build/c64/ur.prg / ur.d64     (cc65)
make apple2        # -> build/apple2/ur.po            (cc65)
make all           # all four platforms
make clean
```

Then load the image in the matching emulator:

- **Atari:** open `build/atari/ur.atr` (or `ur.xex`) in Altirra.
- **Adam:** load `build/adam/ur.dsk` in MAME's `adam` driver / ADAMEm.
- **C64:** `x64sc build/c64/ur.d64`.
- **Apple II:** open `build/apple2/ur.po` in AppleWin/MAME.

### Network testing

1. Start the game server locally (see [`src/net/CLAUDE.md`](../src/net/CLAUDE.md)
   and `server/` once it exists).
2. Run FujiNet-PC pointed at the emulator.
3. Launch the game in two emulator instances (e.g. two Altirra windows, or Altirra
   + VICE) to exercise a full cross-platform turn loop.

## Disk/image tooling

Produced by the build (and bundled with `fujinet-build-tools` / the toolchains):
`dir2atr` (Atari `.atr`), `z88dk-appmake` (Adam `.dsk`/`.ddp`), `c1541` from VICE
(C64 `.d64`/`.prg`), AppleCommander (Apple `.po`/`.dsk`).

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds the dev
image and runs `make deps`, `make test`, and `make all` on every push — this is
what enforces that `src/common/` stays compilable on both toolchains.

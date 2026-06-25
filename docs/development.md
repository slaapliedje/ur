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

## Setup: toolchains via a container (recommended)

The toolchains live in one reproducible image (the same one CI uses), built from
the repo [`Dockerfile`](../Dockerfile). It is Alpine-based: z88dk comes from the
official image and cc65 is built from source. Emulators are installed **natively**
(next section) because they're interactive GUI apps.

You need a container engine — **Docker or Podman** both work (Podman is a
drop-in, daemonless/rootless alternative; substitute `podman` for `docker`, or
`alias docker=podman`). You do **not** need both, and you don't need a container
at all if you install the toolchains natively (see below).

```sh
# Build the dev image once (cc65 + z88dk + host gcc)
docker build -t ur-dev .

# Run any build/test command inside it (your working copy is bind-mounted at /src)
docker run --rm -it -v "$PWD":/src ur-dev make test
docker run --rm -it -v "$PWD":/src ur-dev make atari
docker run --rm -it -v "$PWD":/src ur-dev          # interactive shell
```

**VS Code:** open the folder and "Reopen in Container" — [`.devcontainer/`](../.devcontainer/devcontainer.json)
uses the same `Dockerfile` (and supports Podman).

> Sanity check inside the container: `cc65 --version`, `cl65 --version`, and `zcc`
> should all run. CI builds this exact image on every push, so breakage shows up there.

### Native toolchain install (alternative)

No container required if you install the toolchains directly:

- **cc65** — `pacman -S cc65` (Arch), `apt install cc65` (Debian/Ubuntu),
  `brew install cc65` (macOS), or build from https://github.com/cc65/cc65.
- **z88dk** — `z88dk` in the AUR (Arch), or build from
  https://github.com/z88dk/z88dk (nightly builds recommended; the `coleco`/`adam`
  target needs a current build). Set `ZCCCFG`/`PATH` per its docs.

The `Makefile` finds tools on your `PATH` (`cl65`, `zcc`, host `cc`), so a native
install and the container are interchangeable for building.

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

### Running the Atari build (`make run-atari`)

Build + launch in one step:

```sh
make run-atari
```

[`tools/run-atari.sh`](../tools/run-atari.sh) auto-detects the emulator:

- **`altirra`** — the AUR Wine wrapper (default if present). Altirra is a Windows
  app, so the script converts the path with `winepath -w` and passes it through.
  Manual equivalent: `altirra "$(winepath -w build/atari/ur.xex)"`.
- **`AltirraSDL`** — a native Linux build (no Wine). Force it with `ALTIRRA_SDL=1`.
  Manual equivalent: `AltirraSDL build/atari/ur.xex`.
- a standalone Windows Altirra `.exe` — set `ALTIRRA=/path/to/Altirra64.exe`.

```sh
make run-atari                    # uses the `altirra` Wine wrapper
ALTIRRA_SDL=1 make run-atari       # use the native AltirraSDL instead
ALTIRRA_OPTS="/ntsc" make run-atari
```

> The AUR `altirra` wrapper sets its own `WINEPREFIX=~/.altirra/wine` and runs
> `wine /opt/altirra/Altirra64.exe "$@"`, so it just needs a Windows-style path.

### Network testing

**N: TCP smoke test (Phase 1).** The Atari build does a TCP echo against a local
server through FujiNet-PC:

1. Start the echo server on your machine: `python3 tools/echo-server.py` (port 1234).
2. Start **FujiNet-PC** (it bridges the emulated Atari's SIO to your real network).
3. `make atari`, then boot `build/atari/ur.xex` in **Altirra** (configured to use
   FujiNet-PC). The program prints the adapter config, sends `HELLO UR`, and should
   show the echoed reply (`recv: HELLO UR`).
   - Change the endpoint in `src/atari/main.c` (`UR_NET_URL`) for a different host/port.

**Full multiplayer (later).** Once the game server exists, run it locally, point the
clients at it via `N:TCP://...`, and drive two emulator instances (e.g. two Altirra
windows, or Altirra + VICE) to exercise a full cross-platform turn loop.

## Disk/image tooling

Produced by the build (and bundled with `fujinet-build-tools` / the toolchains):
`dir2atr` (Atari `.atr`), `z88dk-appmake` (Adam `.dsk`/`.ddp`), `c1541` from VICE
(C64 `.d64`/`.prg`), AppleCommander (Apple `.po`/`.dsk`).

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds the dev
image and runs `make deps`, `make test`, and `make all` on every push — this is
what enforces that `src/common/` stays compilable on both toolchains.

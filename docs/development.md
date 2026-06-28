# Development guide

How to set up tools, build each platform, and run/test the game. See
[`/CLAUDE.md`](../CLAUDE.md) for the architecture this guide builds.

> **Status:** the game is **playable on every target** (local hot-seat + vs-AI),
> with FujiNet online on the four FujiNet platforms. See [`ROADMAP.md`](../ROADMAP.md)
> for the per-phase / per-port status.

## The two-toolchain reality

Some targets are 6502 and some are Z80-family, so there are **two C toolchains**:

| Toolchain | CPU | Targets | Notes |
|-----------|-----|---------|-------|
| [cc65](https://cc65.github.io/) | 6502 | Atari, Atari 5200, C64, Apple II, NES | `cl65` drives compile+link |
| [z88dk](https://github.com/z88dk/z88dk) | Z80 / Z80-like | Coleco Adam, ColecoVision, SMS, Game Gear, Game Boy | `zcc` (zsdcc/sccz80) + `z88dk-appmake` |

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

- **cc65** — `fujinet-lib`'s prebuilt libraries are built with **current** cc65,
  which renamed the C-stack zeropage symbol `sp` → `c_sp`. The last cc65 *release*
  is 2.19 (2020), which predates that, so linking the FujiNet `N:`/fuji code
  against it fails with `Unresolved external 'c_sp'`. Two ways to fix it:
  - **Keep cc65 2.19 — handled automatically.** The build checks the cc65 runtime
    and, on 2.19, assembles `src/atari/csp_compat.s` (aliasing `c_sp` to its `sp`),
    so `make atari` / `make run-atari` just work. Override with `CSP_COMPAT=0` or `=1`.
  - **Or build current cc65 from source** (what CI uses):
    `git clone https://github.com/cc65/cc65 && cd cc65 && make && sudo make install PREFIX=/usr/local`.
    Note: the last *release* is 2.19 and the AUR `cc65-git` is pinned to an even
    older commit — neither has `c_sp`; only upstream **master** does.
  - (cc65 2.19 links everything *except* the FujiNet network/fuji code fine.)
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
# --- FujiNet platforms (local + online) ---
make atari         # -> build/atari/ur.xex / ur.atr   (cc65)
make adam          # -> build/adam/ur.ddp             (z88dk)
make c64           # -> build/c64/ur.prg / ur.d64     (cc65)
make apple2        # -> build/apple2/ur.system / ur.po (cc65)
# --- bonus console / handheld ports (local only) ---
make coleco        # -> build/coleco/ur.rom           (z88dk; ColecoVision cart)
make sms           # -> build/sms/ur.sms              (z88dk)
make gamegear      # -> build/sms/ur-gg.gg            (z88dk; -DUR_GG)
make gb            # -> build/gb/ur.gb                (z88dk; GB + GBC dual-mode)
make a5200         # -> build/a5200/ur.a52            (cc65; -DUR_A5200)
make nes           # -> build/nes/ur.nes              (cc65; iNES NROM)
make all           # the four primary platforms
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

- **`AltirraSDL`** — native Linux build (no Wine); used by default if present.
  Manual equivalent: `AltirraSDL build/atari/ur.xex`.
- **`altirra`** — the AUR Wine wrapper; force it with `ALTIRRA_WINE=1`. Altirra is a
  Windows app, so the script converts the path with `winepath -w` and passes it
  through. Manual equivalent: `altirra "$(winepath -w build/atari/ur.xex)"`.
- a standalone Windows Altirra `.exe` — set `ALTIRRA=/path/to/Altirra64.exe`.

```sh
make run-atari                     # native AltirraSDL (default)
ALTIRRA_WINE=1 make run-atari       # full Windows Altirra via the Wine wrapper
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

**Online multiplayer (2 players).** The game has an "Online (FujiNet)" mode that
plays against the Go game server in [`server/`](../server/CLAUDE.md).

1. Build and run the server (needs Go — `pacman -S go`):
   ```sh
   cd server && go build -o ur-server . && ./ur-server   # listens on :1234
   ```
2. Run **FujiNet-PC** and an emulator that talks to it. Note: the native
   **AltirraSDL has no FujiNet** — for networking use FujiNet-PC with the Windows
   Altirra (via the `altirra` Wine wrapper) or `atari800`, or real FujiNet hardware.
3. Boot `build/atari/ur.xex` in **two** instances; in each pick **3) Online**. They
   connect to `N:TCP://localhost:1234/` (the `UR_NET_URL` define), the server seats
   them as Light/Dark, and mediates the game.

Without FujiNet attached, selecting "Online" simply shows "connect failed" — which
still confirms the client's network path runs.

**One-command harness.** [`tools/online-test.sh`](../tools/online-test.sh) launches
the server + N Atari/FujiNet-PC pairs (default 2 = a full match) and cleans up on
Ctrl-C:

```sh
tools/online-test.sh        # server + 2 atari800+FujiNet-PC pairs; pick "3) Online" in each
tools/online-test.sh 1      # one pair (connectivity test; it JOINs and waits)
```

Prerequisites it expects (and tells you how to get if missing): **atari800** with
NetSIO (AUR `atari800` 6.1.0 has it), a built **FujiNet-PC** (`FUJINET_BIN`, default
`~/dev/fujinet-pc/build/fujinet`), a built **`ur.xex`**, and **Go** for the server.
On a current toolchain, build FujiNet-PC with
[`tools/patches/fujinet-pc-gcc16-glibc.patch`](../tools/patches/README.md). atari800
windowing uses `-windowed -win-width N -win-height N` (override via `WIN_W`/`WIN_H`).

**Testing the FujiNet lobby.** [`tools/run-lobby.sh`](../tools/run-lobby.sh) boots
atari800 + FujiNet-PC into the **FujiNet CONFIG menu** (cold boot, no disk) so you
can browse the public lobby — the registered "Royal Game of Ur" should appear and,
when selected, download the client over TNFS and launch it. Pass a client image
(`tools/run-lobby.sh path/to/app.xex`) to boot a specific app, e.g. a dedicated
lobby client. See [`docs/hosting.md`](hosting.md) for the server side.

## Disk/image tooling

Produced by the build (and bundled with `fujinet-build-tools` / the toolchains):
`dir2atr` (Atari `.atr`), `z88dk-appmake` (Adam `.dsk`/`.ddp`), `c1541` from VICE
(C64 `.d64`/`.prg`), AppleCommander (Apple `.po`/`.dsk`).

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds the dev
image and runs `make deps`, `make test`, and `make all` on every push — this is
what enforces that `src/common/` stays compilable on both toolchains.

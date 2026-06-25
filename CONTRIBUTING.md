# Contributing to Ur

Thanks for your interest! This is a cross-platform 8-bit game; a little structure
keeps it buildable on every target.

## Getting set up

See [`docs/development.md`](docs/development.md) for tools (cc65, z88dk, emulators)
and the build/run/test loop. The quick version:

```sh
docker build -t ur-dev .
docker run --rm -it -v "$PWD":/src ur-dev make test   # host unit tests
docker run --rm -it -v "$PWD":/src ur-dev make all    # build all platforms
```

## Architecture rules (please respect these)

- **`src/common/` is portable and toolchain-neutral.** No platform headers, no
  hardware access, no `#ifdef PLATFORM`, and no cc65- or z88dk-specific extensions
  — it must compile under **both** toolchains. CI enforces this.
- Platform-specific code lives in `src/atari`, `src/c64`, `src/apple2`, `src/adam`,
  behind the [`src/common/plat.h`](src/common/plat.h) interface. The core calls the
  platform, never the reverse.
- The wire protocol ([`docs/protocol.md`](docs/protocol.md)) is the cross-platform
  contract — change it deliberately and keep client and server in sync.
- Read the relevant `CLAUDE.md` in the directory you're touching first.

## Coding style

- Follow [`.editorconfig`](.editorconfig) (4-space C, tabs in Makefiles/asm/Go).
- Match the surrounding code. Prefer `uint8_t` and fixed-size data; RAM is tiny.
- Keep the core deterministic (same inputs ⇒ same state).

## Licensing

This project is **GPLv3** ([`LICENSE`](LICENSE)). New source files should start
with:

```
/* SPDX-License-Identifier: GPL-3.0-or-later */
```

By contributing, you agree your contributions are licensed under GPLv3.

## Commits & pull requests

- Write clear, imperative commit messages ("Add Atari board renderer").
- Keep PRs focused; note which platform(s) you built/tested and how.
- Make sure `make test` passes and the relevant platform builds before opening a PR.
- Map work to a [`ROADMAP.md`](ROADMAP.md) phase / a GitHub issue where it helps.

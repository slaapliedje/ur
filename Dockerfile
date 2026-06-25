# Dev container for the Ur project — both retro C toolchains in one image:
#   - cc65   : 6502 targets (Atari 8-bit, Commodore 64, Apple II)
#   - z88dk   : Z80 target (Coleco Adam)
# ...plus git/make and a host C compiler for the common/ unit tests.
#
# Emulators (Altirra, VICE, MAME, AppleWin, FujiNet-PC) are GUI tools and are
# installed NATIVELY on your machine, not in this container. See docs/development.md.
#
# This image is the single source of truth shared by local dev and CI.
#
# NOTE: starting point — validate on first build. The official z88dk image is
# Debian-based, so apt is available to add cc65. If that ever changes, switch to a
# multi-stage build that does `COPY --from=z88dk/z88dk /opt/z88dk /opt/z88dk` and
# sets ZCCCFG / PATH manually.
FROM z88dk/z88dk:latest

# cc65 (6502 toolchain) + host build/test tooling. (z88dk is already on PATH,
# with ZCCCFG configured, in the base image.)
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cc65 \
      build-essential \
      git \
      make \
      curl \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Default to an interactive shell; CI overrides the command with `make ...`.
CMD ["/bin/bash"]

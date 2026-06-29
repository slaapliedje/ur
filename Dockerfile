# Dev container for the Ur project — both retro C toolchains in one image:
#   - z88dk : Z80 target (Coleco Adam)              [provided by the base image]
#   - cc65   : 6502 targets (Atari, C64, Apple II)   [built from source below]
# ...plus a host C compiler (gcc, via build-base) for the common/ unit tests.
#
# Emulators (Altirra, VICE, MAME, AppleWin, FujiNet-PC) are GUI tools installed
# NATIVELY on your machine, not in this container. See docs/development.md.
#
# The official z88dk image is ALPINE-based (musl), so we use `apk` and build cc65
# from source — cc65 is not packaged for Alpine. This image is the single source
# of truth shared by local dev and CI. Works with Docker or Podman.
FROM z88dk/z88dk:latest

# build-base = gcc + musl-dev + binutils (also used to run the host unit tests).
# perl = the Game Boy CGB header patch (makefiles/gb-cgb-patch.pl).
RUN apk add --no-cache build-base git make curl ca-certificates unzip perl \
 && git clone --depth 1 https://github.com/cc65/cc65.git /tmp/cc65 \
 && make -C /tmp/cc65 -j4 \
 && make -C /tmp/cc65 install PREFIX=/usr/local \
 && rm -rf /tmp/cc65

# Help the cc65 driver find its target libraries/includes.
ENV CC65_HOME=/usr/local/share/cc65

WORKDIR /src
CMD ["/bin/sh"]

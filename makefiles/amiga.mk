# Commodore Amiga — 68000 / OCS, built with the AmigaPorts m68k-amigaos-gcc
# (bebbo's toolchain) into an AmigaDOS hunk executable, then packaged onto a
# self-booting ADF with amitools' xdftool. Kickstart 1.3-safe (-mcrt=nix13);
# runs in MAME (a500), amiberry, FS-UAE, WinUAE, or real hardware.
#
# Toolchain (not assumed installed — see docs/development.md):
#   AMIGA_CC -> m68k-amigaos-gcc (git clone AmigaPorts/m68k-amigaos-gcc;
#               make all PREFIX=... — needs no root; gdb needn't build)
#   XDFTOOL  -> amitools' xdftool (pip install amitools, any venv)
AMIGA_OUT   := $(BUILD_DIR)/amiga
AMIGA_CC    ?= $(HOME)/dev/toolchains/amigaos/bin/m68k-amigaos-gcc
XDFTOOL     ?= $(HOME)/dev/toolchains/pyenv/bin/xdftool

AMIGA_FLAGS := -mcrt=nix13 -m68000 -O2 -Wall -Wextra \
               -I$(SRC_DIR)/amiga -I$(SRC_DIR)/sms $(COMMON_INC)
AMIGA_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/amiga/*.c)

.PHONY: amiga
amiga: | $(AMIGA_OUT) ## Build the Commodore Amiga executable + bootable ADF
ifeq ($(wildcard $(AMIGA_CC)),)
	@echo "[amiga] m68k-amigaos-gcc not found (set AMIGA_CC; see docs/development.md) — skipping."
else
	$(AMIGA_CC) $(AMIGA_FLAGS) -o $(AMIGA_OUT)/ur $(AMIGA_SOURCES) -lamiga
	rm -f $(AMIGA_OUT)/ur.adf
	$(XDFTOOL) $(AMIGA_OUT)/ur.adf create + format "UR" ofs + boot install boot1x \
	  + write $(AMIGA_OUT)/ur ur + makedir s \
	  + write $(SRC_DIR)/amiga/startup-sequence s/startup-sequence
	@echo "[amiga] built $(AMIGA_OUT)/ur (hunk exe) + ur.adf — MAME a500 / amiberry / FS-UAE"
endif

$(AMIGA_OUT):
	mkdir -p $@

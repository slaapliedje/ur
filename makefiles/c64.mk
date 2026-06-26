# Commodore 64 — 6502/6510, built with cc65 (cl65).
C64_OUT     := $(BUILD_DIR)/c64
CL65        ?= cl65
# C (game) + .s (the sprite multiplexer, src/c64/mux.s).
C64_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) \
               $(wildcard $(SRC_DIR)/c64/*.c) $(wildcard $(SRC_DIR)/c64/*.s)

C64_FLAGS   := -t c64 -O -I$(SRC_DIR)/c64 $(COMMON_INC)
# `make c64 CHARSET=1` builds the charset board (no sprites/raster IRQ) fallback.
ifeq ($(CHARSET),1)
C64_FLAGS   += -DUR_CHARSET
endif

# fujinet-lib (downloaded release): same N: API + wire protocol as the Atari/Adam.
C64_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/c64
C64_LIB       := $(C64_FNLIB_DIR)/fujinet-c64-$(FNLIB_VERSION).lib
C64_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-c64-$(FNLIB_VERSION).zip

# `make c64 ONLINE=1` links fujinet-lib and compiles the FujiNet online path. It is
# OPT-IN because the lib adds ~8K: the program then fills VIC bank 0, leaving no room
# for the 2K custom charset at $3800 (the screen must stay at $0400 for conio). So
# online builds use the ROM charset (board cells as colour tiles) + the multicolor
# sprite tokens; local builds keep the custom-charset showcase. Both link the c_sp
# shim (src/atari/csp_compat.s, plain 6502) on an old cc65.
ifeq ($(ONLINE),1)
C64_FLAGS   += -DUR_ONLINE --include-dir $(C64_FNLIB_DIR)
C64_DEPS    := $(C64_LIB)
C64_LINK    := $(C64_LIB)
ifeq ($(CSP_COMPAT),1)
C64_SOURCES += $(SRC_DIR)/atari/csp_compat.s
endif
endif

.PHONY: c64
c64: $(C64_DEPS) | $(C64_OUT) ## Build the Commodore 64 target (.prg/.d64)
	$(CL65) $(C64_FLAGS) -o $(C64_OUT)/ur.prg $(C64_SOURCES) $(C64_LINK)
	@echo "[c64] built $(C64_OUT)/ur.prg — run in VICE (x64sc); ONLINE=1 adds FujiNet"

# Download + unpack the pinned fujinet-lib release for c64.
$(C64_LIB):
	@mkdir -p $(C64_FNLIB_DIR)
	curl -fsSL $(C64_FNLIB_URL) -o $(C64_FNLIB_DIR)/fujinet-lib.zip
	cd $(C64_FNLIB_DIR) && unzip -o -q fujinet-lib.zip
	@test -f $(C64_LIB) || { echo "ERROR: $(C64_LIB) missing after unzip"; exit 1; }

$(C64_OUT):
	mkdir -p $@

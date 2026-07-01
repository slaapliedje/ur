# Atari ST — 68000, the first 16-bit port. Built with m68k-atari-mint-gcc into a
# GEMDOS .prg; run in Hatari (EmuTOS) or MAME (st). The shared src/common core
# compiles unchanged under GCC for the 68000 (same brain as the 6502/Z80 ports).
ST_OUT     := $(BUILD_DIR)/st
ST_CC      ?= m68k-atari-mint-gcc

# Uses the shared controller (ur_game.c via $(UR_GAME_SRC)); font8.h is shared from
# src/sms (-I). Full GCC, so no toolchain-specific flags needed.
# `make st`           -> plain ST: 320x200 16-colour planar (ur.prg).
# `make st FALCON=1`  -> enhanced Atari Falcon: 320x200 TRUECOLOR chunky (ur-falcon.prg).
ST_DEFS    :=
ST_PRG     := ur.prg
ifeq ($(FALCON),1)
ST_DEFS    += -DUR_FALCON
ST_PRG     := ur-falcon.prg
endif
ST_FLAGS   := -std=c99 -O2 -Wall -Wextra $(ST_DEFS) -I$(SRC_DIR)/st -I$(SRC_DIR)/sms $(COMMON_INC)
ST_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/st/*.c)

.PHONY: st
st: | $(ST_OUT) ## Build the Atari ST (68000; FALCON=1 for the Falcon truecolor build)
ifeq ($(strip $(wildcard $(SRC_DIR)/st/*.c)),)
	@echo "[st] no sources yet — add src/st/*.c. (skeleton)"
else
	$(ST_CC) $(ST_FLAGS) -o $(ST_OUT)/$(ST_PRG) $(ST_SOURCES)
	@echo "[st] built $(ST_OUT)/$(ST_PRG) — Hatari (ST: EmuTOS / Falcon: TOS4 --machine falcon)"
endif

$(ST_OUT):
	mkdir -p $@

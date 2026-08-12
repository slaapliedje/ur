# MS-DOS / IBM PC — x86 real mode, the fourth CPU family (after 6502, Z80, 68000).
# Built with Open Watcom v2 (owcc -bdos, small model) into a 16-bit MZ .exe:
# VGA mode 13h (320x200x256 — the shared layout with TT-style gradient ramps) +
# PC speaker. Run in DOSBox. fujinet-lib has an msdos target built with this same
# toolchain (wcc/wlib), so a FujiNet online build is a real future option.
DOS_OUT    := $(BUILD_DIR)/dos
WATCOM_DIR ?= $(HOME)/dev/toolchains/watcom
DOS_CC     ?= $(WATCOM_DIR)/binl64/owcc
# owcc drives wcc/wlink from PATH and finds headers/libs via WATCOM + INCLUDE.
DOS_ENV    := WATCOM=$(WATCOM_DIR) INCLUDE=$(WATCOM_DIR)/h PATH=$(WATCOM_DIR)/binl64:$$PATH
DOS_FLAGS  := -bdos -mcmodel=s -O2 -fno-stack-check -DUR_DOS \
              -I$(SRC_DIR)/dos -I$(SRC_DIR)/sms $(COMMON_INC)
DOS_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/dos/*.c)

.PHONY: dos msdos
dos: | $(DOS_OUT) ## Build the MS-DOS target (Open Watcom -> ur.exe; VGA + PC speaker)
ifeq ($(strip $(wildcard $(DOS_CC))),)
	@echo "[dos] Open Watcom not found at $(WATCOM_DIR) — skipping. See docs/development.md."
else
	@mkdir -p $(DOS_OUT)/obj
	@set -e; objs=""; for f in $(DOS_SOURCES); do \
	  o=$(DOS_OUT)/obj/$$(basename $$f .c).o; \
	  echo "owcc $$f"; \
	  $(DOS_ENV) $(DOS_CC) $(DOS_FLAGS) -c -o $$o $$f; \
	  objs="$$objs $$o"; \
	done; \
	$(DOS_ENV) $(DOS_CC) -bdos -mcmodel=s -o $(DOS_OUT)/ur.exe $$objs
	@echo "[dos] built $(DOS_OUT)/ur.exe — run: dosbox $(DOS_OUT)/ur.exe"
endif

msdos: dos ## Alias for the MS-DOS target

$(DOS_OUT):
	mkdir -p $@

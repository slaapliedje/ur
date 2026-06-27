# Atari 5200 — 6502, cc65 (-t atari5200). Offline cartridge ROM (no FujiNet).
# UNIFIED with the A8: builds the SAME src/atari sources with -DUR_A5200, which
# strips FujiNet/online/keyboard and swaps the OS-dependent bits for direct
# hardware (POKEY $E800, GTIA colour shadows $0C-$10, a custom 40-col display +
# conio in a5200scr.c, 5200 controller input). The carved board, charset, POKEY
# sound + the Hurrian Hymn are shared verbatim. No dli.s (DLI is a no-op for now),
# no fujinet-lib. Output: build/a5200/ur.a52.  $(CL65) comes from atari.mk.
A5200_OUT     := $(BUILD_DIR)/a5200
# 32K cart ($8000): the shared renderer + cprintf/vsprintf + joy driver overflow 16K.
A5200_FLAGS   := -t atari5200 -DUR_A5200 --asm-define UR_A5200 -Osir -I$(SRC_DIR)/atari \
                 $(COMMON_INC) -Wl -D,__CARTSIZE__=0x8000
A5200_SOURCES := $(COMMON_SOURCES) $(wildcard $(SRC_DIR)/atari/*.c)
A5200_SOURCES += $(SRC_DIR)/atari/dli.s   # board-sheen DLI (UR_A5200: GTIA at $C000)

.PHONY: a5200
a5200: | $(A5200_OUT) ## Build the Atari 5200 cartridge ROM (unified src/atari, -DUR_A5200)
	$(CL65) $(A5200_FLAGS) -o $(A5200_OUT)/ur.a52 $(A5200_SOURCES)
	@echo "[a5200] built $(A5200_OUT)/ur.a52 — run in MAME (a5200) / atari800 -5200"

$(A5200_OUT):
	mkdir -p $@

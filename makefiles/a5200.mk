# Atari 5200 — 6502, built with cc65 (-t atari5200). Offline cartridge ROM (no
# FujiNet). Reuses the shared core + the Atari POKEY sound / Hurrian-Hymn approach
# (the 5200 is Atari-8-bit hardware; POKEY lives at $E800, GTIA at $C000). Uses
# $(CL65) from atari.mk (included before this). Output: build/a5200/ur.a52.
A5200_OUT     := $(BUILD_DIR)/a5200
A5200_FLAGS   := -t atari5200 -Osir -I$(SRC_DIR)/a5200 $(COMMON_INC)
A5200_SOURCES := $(COMMON_SOURCES) $(wildcard $(SRC_DIR)/a5200/*.c)

.PHONY: a5200
a5200: | $(A5200_OUT) ## Build the Atari 5200 cartridge ROM (cc65 -t atari5200)
	$(CL65) $(A5200_FLAGS) -o $(A5200_OUT)/ur.a52 $(A5200_SOURCES)
	@echo "[a5200] built $(A5200_OUT)/ur.a52 — run in MAME (a5200) / atari800 -5200"

$(A5200_OUT):
	mkdir -p $@

# Nintendo Entertainment System / Famicom — 6502 (Ricoh 2A03), built with cc65.
#
# The NES is a 6502 machine, so it reuses the shared C core via cc65 (just like the
# Atari/C64/Apple II). cc65's `nes` target emits a standard iNES NROM cartridge
# (16K PRG + 8K CHR, mapper 0). There is NO FujiNet for the NES, so this is a
# LOCAL-ONLY build (hot-seat + vs-AI), like the ColecoVision cartridge.
NES_OUT     := $(BUILD_DIR)/nes
CL65        ?= cl65
# C (game) + any .s (hand-tuned asm) under src/nes.
NES_SOURCES := $(COMMON_SOURCES) \
               $(wildcard $(SRC_DIR)/nes/*.c) $(wildcard $(SRC_DIR)/nes/*.s)
NES_FLAGS   := -t nes -O -I$(SRC_DIR)/nes $(COMMON_INC)

.PHONY: nes
nes: | $(NES_OUT) ## Build the NES / Famicom target (.nes iNES ROM)
	$(CL65) $(NES_FLAGS) -o $(NES_OUT)/ur.nes $(NES_SOURCES)
	@echo "[nes] built $(NES_OUT)/ur.nes — run in MAME (nes) / Mesen / FCEUX"

$(NES_OUT):
	mkdir -p $@

# Nintendo Game Boy / Game Boy Color — Sharp LR35902, built with z88dk (+gb,
# gbz80 backend + gb_clib). ONE cart that runs on both: colour on a Game Boy Color
# (CGB palettes via <arch/gb/cgb.h>, gated on _cpu==CGB_TYPE), 4 greys on a DMG.
# The GB screen is 160x144 = 20x18 tiles, so it reuses the SMS port's compact
# layout + procedural board art (2bpp tiles). font8.h is shared from src/sms.
# Uses $(ZCC) + the exported ZCCCFG from adam.mk (include after it). No FujiNet.
GB_OUT     := $(BUILD_DIR)/gb
GB_FLAGS   := +gb -DUR_GB -I$(SRC_DIR)/gb -I$(SRC_DIR)/sms $(COMMON_INC)
GB_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/gb/*.c)

.PHONY: gb
gb: | $(GB_OUT) ## Build the Game Boy / Game Boy Color ROM (z88dk +gb)
	$(ZCC) $(GB_FLAGS) $(GB_SOURCES) -o $(GB_OUT)/ur -create-app
	@perl makefiles/gb-cgb-patch.pl $(GB_OUT)/ur.gb
	@echo "[gb] built $(GB_OUT)/ur.gb — run in MAME (gameboy = grey / gbcolor = colour)"

$(GB_OUT):
	mkdir -p $@

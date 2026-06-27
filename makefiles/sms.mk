# Sega Master System — Z80, built with z88dk (+sms). Offline cartridge ROM (no
# FujiNet). Reuses the Z80 / TMS9918-derived VDP / SN76489 approach from the
# Adam/ColecoVision layer (src/adam). Uses $(ZCC) and the exported ZCCCFG from
# adam.mk (this fragment is included after it). Output: build/sms/ur.sms.
SMS_OUT     := $(BUILD_DIR)/sms
SMS_FLAGS   := +sms -DUR_SMS -I$(SRC_DIR)/sms $(COMMON_INC)
SMS_SOURCES := $(COMMON_SOURCES) $(wildcard $(SRC_DIR)/sms/*.c)

.PHONY: sms
sms: | $(SMS_OUT) ## Build the Sega Master System ROM (z88dk +sms)
	$(ZCC) $(SMS_FLAGS) $(SMS_SOURCES) -o $(SMS_OUT)/ur -create-app
	@echo "[sms] built $(SMS_OUT)/ur.sms — run in MAME (sms driver) / Emulicious"

# Game Gear falls out of the same code via the gamegear subtype (SMS-family VDP).
.PHONY: gamegear
gamegear: | $(SMS_OUT) ## Build the Game Gear ROM (same code, gamegear subtype)
	$(ZCC) $(SMS_FLAGS) -subtype=gamegear $(SMS_SOURCES) -o $(SMS_OUT)/ur-gg -create-app
	@echo "[gamegear] built $(SMS_OUT)/ur-gg.gg — run in MAME (gamegear driver)"

$(SMS_OUT):
	mkdir -p $@

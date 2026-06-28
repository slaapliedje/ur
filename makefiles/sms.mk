# Sega Master System — Z80, built with z88dk (+sms). Offline cartridge ROM (no
# FujiNet). The SMS native display is VDP Mode 4 (a 4bpp tilemap). Plain conio
# text did NOT show up (the earlier scaffold black-screened: the classic console
# defaults to an invisible ink). We render with z88dk's classic <sms.h> Mode-4 VDP
# API instead (load_palette / load_tiles / set_bkg_map / read_joypad1 — all in the
# installed sms_clib; the devkitSMS SMSlib proper and the newlib sms.lib are NOT
# packaged here, only sccz80 + sms_clib). Default clib (-clib=default) links
# sms_clib and the Mode-4 CRT; we just fix the palette + enable the display in C.
# Output: build/sms/ur.sms. Uses $(ZCC) + exported ZCCCFG from adam.mk.
SMS_OUT     := $(BUILD_DIR)/sms
SMS_FLAGS   := +sms -DUR_SMS -I$(SRC_DIR)/sms $(COMMON_INC)
SMS_SOURCES := $(COMMON_SOURCES) $(wildcard $(SRC_DIR)/sms/*.c)

.PHONY: sms
sms: | $(SMS_OUT) ## Build the Sega Master System ROM (z88dk +sms, SMSlib)
	$(ZCC) $(SMS_FLAGS) $(SMS_SOURCES) -o $(SMS_OUT)/ur -create-app
	@echo "[sms] built $(SMS_OUT)/ur.sms — run in MAME (sms driver) / Emulicious"

# Game Gear: same SMS-family VDP + code, but only a 160x144 (20x18-tile) centre
# window is visible, so -DUR_GG selects a compacted layout (src/sms/main.c).
.PHONY: gamegear
gamegear: | $(SMS_OUT) ## Build the Game Gear ROM (same code, gamegear subtype)
	$(ZCC) $(SMS_FLAGS) -subtype=gamegear -DUR_GG $(SMS_SOURCES) -o $(SMS_OUT)/ur-gg -create-app
	@echo "[gamegear] built $(SMS_OUT)/ur-gg.gg — run in MAME (gamegear driver)"

$(SMS_OUT):
	mkdir -p $@

# Coleco Adam — Z80, built with z88dk (zcc). NOT cc65.
ADAM_OUT      := $(BUILD_DIR)/adam
ZCC          ?= zcc
ADAM_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/adam/*.c)

# fujinet-lib (downloaded release) for adam. The lib is built with z88dk's default
# (classic / sccz80) C library for `+coleco -subtype=adam`, so we link with the
# matching defaults — do NOT pass -clib=sdcc / -compiler=sdcc here.
ADAM_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/adam
ADAM_LIB       := $(ADAM_FNLIB_DIR)/fujinet-adam-$(FNLIB_VERSION).lib
ADAM_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-adam-$(FNLIB_VERSION).zip

# z88dk target: Coleco with the `adam` subtype (matches how the lib was built).
ADAM_FLAGS   := +coleco -subtype=adam -I$(ADAM_FNLIB_DIR) $(COMMON_INC)

# z88dk's -l takes the library filename WITH the .lib extension (unlike gcc/cc65),
# e.g. -leos.lib. The Adam also needs z88dk's eos (OS) + smartkeys (keyboard) libs
# for stdio (printf/getchar). This matches the canonical fujinet Adam app build.
.PHONY: adam
adam: $(ADAM_LIB) | $(ADAM_OUT) ## Build the Coleco Adam target (z88dk)
	$(ZCC) $(ADAM_FLAGS) $(ADAM_SOURCES) -o $(ADAM_OUT)/ur \
		-L$(ADAM_FNLIB_DIR) -lfujinet-adam-$(FNLIB_VERSION).lib \
		-leos.lib -lsmartkeys.lib -create-app
	@echo "[adam] built in $(ADAM_OUT)/ — run in MAME (adam driver) / ADAMEm"

# Download + unpack the pinned fujinet-lib release for adam.
$(ADAM_LIB):
	@mkdir -p $(ADAM_FNLIB_DIR)
	curl -fsSL $(ADAM_FNLIB_URL) -o $(ADAM_FNLIB_DIR)/fujinet-lib.zip
	cd $(ADAM_FNLIB_DIR) && unzip -o -q fujinet-lib.zip
	@test -f $(ADAM_LIB) || { echo "ERROR: $(ADAM_LIB) missing after unzip"; exit 1; }

$(ADAM_OUT):
	mkdir -p $@

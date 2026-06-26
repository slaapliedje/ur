# Coleco Adam — Z80, built with z88dk (zcc). NOT cc65.
ADAM_OUT      := $(BUILD_DIR)/adam
ZCC          ?= zcc
ADAM_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/adam/*.c)

# fujinet-lib (downloaded release) for adam. Built with z88dk's default (classic /
# sccz80) C library for `+coleco -subtype=adam`, so we link with matching defaults.
ADAM_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/adam
ADAM_LIB       := $(ADAM_FNLIB_DIR)/fujinet-adam-$(FNLIB_VERSION).lib
ADAM_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-adam-$(FNLIB_VERSION).zip

# fujinet-adam.lib does its AdamNet I/O through EOS (eos_write_character_device,
# ...), provided by tschak909's eoslib — a community z88dk lib not in base z88dk.
# Build it from source to get eos.lib. (smartkeys is NOT referenced, so skip it.)
EOSLIB_SRC  := $(LIB_DIR)/eoslib
EOSLIB      := $(EOSLIB_SRC)/eos.lib
EOSLIB_REPO := https://github.com/tschak909/eoslib.git

# z88dk's -l takes the library filename WITH the .lib extension (unlike gcc/cc65).
ADAM_FLAGS  := +coleco -subtype=adam -I$(ADAM_FNLIB_DIR) -I$(SRC_DIR)/adam $(COMMON_INC)

.PHONY: adam
adam: $(ADAM_LIB) $(EOSLIB) | $(ADAM_OUT) ## Build the Coleco Adam target (z88dk)
	$(ZCC) $(ADAM_FLAGS) $(ADAM_SOURCES) -o $(ADAM_OUT)/ur \
		-L$(ADAM_FNLIB_DIR) -L$(EOSLIB_SRC) \
		-lfujinet-adam-$(FNLIB_VERSION).lib -leos.lib -create-app
	@echo "[adam] built in $(ADAM_OUT)/ — run in MAME (adam driver) / ADAMEm"

# Download + unpack the pinned fujinet-lib release for adam.
$(ADAM_LIB):
	@mkdir -p $(ADAM_FNLIB_DIR)
	curl -fsSL $(ADAM_FNLIB_URL) -o $(ADAM_FNLIB_DIR)/fujinet-lib.zip
	cd $(ADAM_FNLIB_DIR) && unzip -o -q fujinet-lib.zip
	@test -f $(ADAM_LIB) || { echo "ERROR: $(ADAM_LIB) missing after unzip"; exit 1; }

# eoslib's Makefile needs Z88DK_SHARE (the dir holding lib/config). Use ZCCCFG
# when it's set (CI / per-user installs), otherwise find the system z88dk (e.g.
# a distro install at /usr/share/z88dk, where ZCCCFG is unset and zcc uses a
# built-in default).
Z88DK_SHARE := $(if $(ZCCCFG),$(patsubst %/lib/config,%,$(ZCCCFG)),$(patsubst %/lib/config/coleco.cfg,%,$(firstword $(wildcard /usr/share/z88dk/lib/config/coleco.cfg /usr/local/share/z88dk/lib/config/coleco.cfg /usr/lib/z88dk/lib/config/coleco.cfg))))

# zcc needs ZCCCFG to find the coleco target config. Set it (and export to all
# recipes) only when the environment hasn't already, so CI/per-user installs win.
ifeq ($(strip $(ZCCCFG)),)
ZCCCFG := $(Z88DK_SHARE)/lib/config
endif
export ZCCCFG

# Clone + build eoslib (z88dk) -> eos.lib.
$(EOSLIB):
	@mkdir -p $(LIB_DIR)
	[ -d $(EOSLIB_SRC) ] || git clone --depth 1 $(EOSLIB_REPO) $(EOSLIB_SRC)
	$(MAKE) -C $(EOSLIB_SRC) Z88DK_SHARE="$(Z88DK_SHARE)"
	@test -f $(EOSLIB) || { echo "ERROR: $(EOSLIB) missing after build"; exit 1; }

$(ADAM_OUT):
	mkdir -p $@

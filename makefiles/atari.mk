# Atari 8-bit — 6502, built with cc65 (cl65 drives compile + link).
ATARI_OUT      := $(BUILD_DIR)/atari
CL65           ?= cl65
ATARI_TARGET   ?= atari        # use 'atarixl' for an XL/XE-only build
ATARI_SOURCES  := $(COMMON_SOURCES) $(UR_GAME_SRC) $(NET_SOURCES) $(wildcard $(SRC_DIR)/atari/*.c)
ATARI_SOURCES  += $(SRC_DIR)/atari/dli.s   # display-list-interrupt handler (title sky)

# Optional shim for cc65 <= 2.19 (exports `sp`, not `c_sp`) so fujinet-lib's
# network code links. Enable with `CSP_COMPAT=1 make ...`; leave off on current cc65.
ifeq ($(CSP_COMPAT),1)
ATARI_SOURCES += $(SRC_DIR)/atari/csp_compat.s
endif

# fujinet-lib (downloaded release): headers are flat in the dir; the lib is named
# per target/version. See makefiles/common.mk for FNLIB_VERSION.
ATARI_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/atari
ATARI_LIB       := $(ATARI_FNLIB_DIR)/fujinet-atari-$(FNLIB_VERSION).lib
ATARI_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-atari-$(FNLIB_VERSION).zip

ATARI_FLAGS    := -t $(ATARI_TARGET) -Osir --include-dir $(ATARI_FNLIB_DIR) $(COMMON_INC)

.PHONY: atari
atari: $(ATARI_LIB) | $(ATARI_OUT) ## Build the Atari 8-bit target (.xex)
	$(CL65) $(ATARI_FLAGS) -o $(ATARI_OUT)/ur.xex $(ATARI_SOURCES) $(ATARI_LIB)
	@echo "[atari] built $(ATARI_OUT)/ur.xex — run in Altirra (FujiNet-PC for networking)"

# Download + unpack the pinned fujinet-lib release for atari.
$(ATARI_LIB):
	@mkdir -p $(ATARI_FNLIB_DIR)
	curl -fsSL $(ATARI_FNLIB_URL) -o $(ATARI_FNLIB_DIR)/fujinet-lib.zip
	cd $(ATARI_FNLIB_DIR) && unzip -o -q fujinet-lib.zip
	@test -f $(ATARI_LIB) || { echo "ERROR: $(ATARI_LIB) missing after unzip"; exit 1; }

$(ATARI_OUT):
	mkdir -p $@

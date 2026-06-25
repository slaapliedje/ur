# Atari 8-bit — 6502, built with cc65 (cl65 drives compile + link).
ATARI_OUT     := $(BUILD_DIR)/atari
CL65          ?= cl65
ATARI_TARGET  ?= atari        # use 'atarixl' for an XL/XE-only build
ATARI_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/atari/*.c)
ATARI_LIB     := $(LIB_DIR)/fujinet-lib/atari/fujinet-atari.lib   # TODO confirm name
ATARI_FLAGS   := -t $(ATARI_TARGET) -O $(COMMON_INC)

.PHONY: atari
atari: | $(ATARI_OUT) ## Build the Atari 8-bit target (.xex/.atr)
ifeq ($(strip $(ATARI_SOURCES)),)
	@echo "[atari] no sources yet — add src/atari/*.c, then build (cc65). (skeleton)"
else
	$(CL65) $(ATARI_FLAGS) -o $(ATARI_OUT)/ur.xex $(ATARI_SOURCES) $(ATARI_LIB)
	@echo "TODO: package $(ATARI_OUT)/ur.xex into ur.atr (dir2atr)"
endif

$(ATARI_OUT):
	mkdir -p $@

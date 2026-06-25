# Apple II — 6502, built with cc65 (cl65).
APPLE2_OUT     := $(BUILD_DIR)/apple2
CL65           ?= cl65
APPLE2_TARGET  ?= apple2        # use 'apple2enh' for the enhanced //e
APPLE2_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/apple2/*.c)
APPLE2_LIB     := $(LIB_DIR)/fujinet-lib/apple2/fujinet-apple2.lib   # TODO confirm name
APPLE2_FLAGS   := -t $(APPLE2_TARGET) -O $(COMMON_INC)

.PHONY: apple2
apple2: | $(APPLE2_OUT) ## Build the Apple II target (.po)
ifeq ($(strip $(APPLE2_SOURCES)),)
	@echo "[apple2] no sources yet — add src/apple2/*.c, then build (cc65). (skeleton)"
else
	$(CL65) $(APPLE2_FLAGS) -o $(APPLE2_OUT)/ur.system $(APPLE2_SOURCES) $(APPLE2_LIB)
	@echo "TODO: package into ur.po (AppleCommander)"
endif

$(APPLE2_OUT):
	mkdir -p $@

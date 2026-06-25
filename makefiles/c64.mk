# Commodore 64 — 6502/6510, built with cc65 (cl65).
C64_OUT     := $(BUILD_DIR)/c64
CL65        ?= cl65
C64_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/c64/*.c)
C64_LIB     := $(LIB_DIR)/fujinet-lib/c64/fujinet-c64.lib   # TODO confirm name
C64_FLAGS   := -t c64 -O $(COMMON_INC)

.PHONY: c64
c64: | $(C64_OUT) ## Build the Commodore 64 target (.prg/.d64)
ifeq ($(strip $(C64_SOURCES)),)
	@echo "[c64] no sources yet — add src/c64/*.c, then build (cc65). (skeleton)"
else
	$(CL65) $(C64_FLAGS) -o $(C64_OUT)/ur.prg $(C64_SOURCES) $(C64_LIB)
	@echo "TODO: package $(C64_OUT)/ur.prg into ur.d64 (c1541)"
endif

$(C64_OUT):
	mkdir -p $@

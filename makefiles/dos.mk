# MS-DOS / IBM PC — x86 real mode, the fourth CPU family (after 6502, Z80, 68000).
# Built with Open Watcom v2 (owcc -bdos, small model) into a 16-bit MZ .exe:
# VGA mode 13h (320x200x256 — the shared layout with TT-style gradient ramps) +
# PC speaker. Run in DOSBox.
#
# FujiNet online is baked into the default binary (like the Atari/Adam): the
# fujinet-lib msdos build is the SAME Open Watcom toolchain, riding INT F5h to
# the fujinet-msdos FUJINET.SYS driver (RS-232 FujiNet). Without the driver the
# Online option fails gracefully. `make dos ONLINE=0` builds the lib-less
# local-only variant.
DOS_OUT    := $(BUILD_DIR)/dos
WATCOM_DIR ?= $(HOME)/dev/toolchains/watcom
DOS_CC     ?= $(WATCOM_DIR)/binl64/owcc
# owcc drives wcc/wlink from PATH and finds headers/libs via WATCOM + INCLUDE.
DOS_ENV    := WATCOM=$(WATCOM_DIR) INCLUDE=$(WATCOM_DIR)/h PATH=$(WATCOM_DIR)/binl64:$$PATH
DOS_FLAGS  := -bdos -mcmodel=s -O2 -fno-stack-check -DUR_DOS \
              -I$(SRC_DIR)/dos -I$(SRC_DIR)/sms $(COMMON_INC)
DOS_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/dos/*.c)

# fujinet-lib (downloaded release): same N: API + wire protocol as the other four.
DOS_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/msdos
DOS_LIB       := $(DOS_FNLIB_DIR)/fujinet-msdos-$(FNLIB_VERSION).lib
DOS_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-msdos-$(FNLIB_VERSION).zip

DOS_LINKLIBS :=
ifneq ($(ONLINE),0)
# __MSDOS__: fujinet-lib's headers gate their msdos structs on it (the lib's own
# build passes it too; wcc predefines MSDOS/__DOS__ but not this spelling).
DOS_FLAGS    += -DUR_ONLINE -D__MSDOS__ -I$(DOS_FNLIB_DIR)
DOS_LINKLIBS := $(DOS_LIB)
dos: $(DOS_LIB)
endif

.PHONY: dos msdos
dos: | $(DOS_OUT) ## Build the MS-DOS target (Open Watcom -> ur.exe; VGA + speaker + FujiNet)
ifeq ($(strip $(wildcard $(DOS_CC))),)
	@echo "[dos] Open Watcom not found at $(WATCOM_DIR) — skipping. See docs/development.md."
else
	@mkdir -p $(DOS_OUT)/obj
	@set -e; objs=""; for f in $(DOS_SOURCES); do \
	  o=$(DOS_OUT)/obj/$$(basename $$f .c).o; \
	  echo "owcc $$f"; \
	  $(DOS_ENV) $(DOS_CC) $(DOS_FLAGS) -c -o $$o $$f; \
	  objs="$$objs $$o"; \
	done; \
	$(DOS_ENV) $(DOS_CC) -bdos -mcmodel=s -o $(DOS_OUT)/ur.exe $$objs $(DOS_LINKLIBS)
	@echo "[dos] built $(DOS_OUT)/ur.exe — run: dosbox $(DOS_OUT)/ur.exe"
endif

msdos: dos ## Alias for the MS-DOS target

# Download + unpack the pinned fujinet-lib release for msdos.
$(DOS_LIB):
	@mkdir -p $(DOS_FNLIB_DIR)
	curl -fsSL $(DOS_FNLIB_URL) -o $(DOS_FNLIB_DIR)/fujinet-lib.zip
	cd $(DOS_FNLIB_DIR) && unzip -o -q fujinet-lib.zip

$(DOS_OUT):
	mkdir -p $@

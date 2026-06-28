# Apple II — 6502, built with cc65 (cl65).
APPLE2_OUT     := $(BUILD_DIR)/apple2
CL65           ?= cl65
# Default: lo-res colour board, ProDOS SYSTEM program ($2000). `make apple2 DHGR=1`
# builds the double-hi-res board instead (140x192, 16 colours) — needs the enhanced
# //e + a custom SYSTEM layout (page-2 bitmap at $4000); see src/apple2/CLAUDE.md.
APPLE2_TARGET  ?= apple2
APPLE2_CFG     ?= apple2-system.cfg
APPLE2_DEFS    :=
# Lo-res excludes the DHGR sources; DHGR=1 adds them (dhgr.c + the asm aux-blit).
APPLE2_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(NET_SOURCES) \
                  $(filter-out %/dhgr.c,$(wildcard $(SRC_DIR)/apple2/*.c))
ifeq ($(DHGR),1)
APPLE2_TARGET  := apple2enh
APPLE2_CFG     := $(SRC_DIR)/apple2/apple2-dhgr.cfg
APPLE2_DEFS    += -DUR_DHGR
APPLE2_SOURCES += $(SRC_DIR)/apple2/dhgr.c $(SRC_DIR)/apple2/dhgr_blit.s
endif

# fujinet-lib (apple2 / SmartPort): same N: API + wire protocol as the other ports.
APPLE2_FNLIB_DIR := $(LIB_DIR)/fujinet-lib/apple2
APPLE2_LIB       := $(APPLE2_FNLIB_DIR)/fujinet-apple2-$(FNLIB_VERSION).lib
APPLE2_FNLIB_URL := https://github.com/FujiNetWIFI/fujinet-lib/releases/download/v$(FNLIB_VERSION)/fujinet-lib-apple2-$(FNLIB_VERSION).zip
APPLE2_LINK :=
APPLE2_DEPS :=
APPLE2_INC  :=
# `make apple2 ONLINE=1` links fujinet-lib + the FujiNet online path (over SmartPort).
ifeq ($(ONLINE),1)
APPLE2_DEFS += -DUR_ONLINE
APPLE2_INC  := --include-dir $(APPLE2_FNLIB_DIR)
APPLE2_LINK := $(APPLE2_LIB)
APPLE2_DEPS := $(APPLE2_LIB)
ifeq ($(CSP_COMPAT),1)
APPLE2_SOURCES += $(SRC_DIR)/atari/csp_compat.s
endif
endif
APPLE2_FLAGS   := -t $(APPLE2_TARGET) -C $(APPLE2_CFG) -O $(APPLE2_DEFS) $(APPLE2_INC) -I$(SRC_DIR)/apple2 $(COMMON_INC)

# DHGR (code pinned at $6000) + fujinet-lib (~8K) overflow the code region, so the
# two are mutually exclusive. Online uses the lo-res board; DHGR is local-only.
ifeq ($(DHGR)$(ONLINE),11)
$(error apple2: DHGR=1 and ONLINE=1 don't fit together (CODE overflows MAIN). Use one: ONLINE=1 (lo-res board) or DHGR=1 (local).)
endif

# Disk packaging (optional): an AppleCommander jar, if present, builds a ProDOS
# .po with the program as UR.SYSTEM. Set AC=path/to/AppleCommander.jar (or drop the
# jar in lib/ or tools/) to enable; otherwise just the SYSTEM binary is built.
AC ?= $(firstword $(wildcard $(LIB_DIR)/AppleCommander*.jar) \
                  $(wildcard tools/AppleCommander*.jar))

.PHONY: apple2
apple2: $(APPLE2_DEPS) | $(APPLE2_OUT) ## Build the Apple II target (SYSTEM binary + .po if AppleCommander present)
ifeq ($(strip $(wildcard $(SRC_DIR)/apple2/*.c)),)
	@echo "[apple2] no sources yet — add src/apple2/*.c, then build (cc65). (skeleton)"
else
	$(CL65) $(APPLE2_FLAGS) -o $(APPLE2_OUT)/ur.exe $(APPLE2_SOURCES) $(APPLE2_LINK)
	# apple2-system.cfg prepends a 58-byte ($3A) EXEHDR ahead of the $2000 entry.
	# ProDOS loads a SYSTEM file at $2000 and JMPs there, so strip the EXEHDR: the
	# remaining image starts with the crt0 ($2000) entry (LDX #$FF / TXS / ...).
	tail -c +59 $(APPLE2_OUT)/ur.exe > $(APPLE2_OUT)/ur.system
	@echo "[apple2] built $(APPLE2_OUT)/ur.system (ProDOS SYSTEM image, entry \$$2000)"
ifeq ($(strip $(AC)),)
	@echo "[apple2] (no AppleCommander jar -> skipping .po; set AC=path/to/AppleCommander.jar)"
else
	@rm -f $(APPLE2_OUT)/ur.po
	java -jar $(AC) -pro140 $(APPLE2_OUT)/ur.po UR >/dev/null
	java -jar $(AC) -p $(APPLE2_OUT)/ur.po UR.SYSTEM sys 0x2000 < $(APPLE2_OUT)/ur.system
	@echo "[apple2] packaged $(APPLE2_OUT)/ur.po — make a bootable disk with tools/apple2-bootdisk.sh"
endif
endif

# Build a *bootable* ProDOS disk by copying a real ProDOS disk (which supplies the
# PRODOS kernel + boot block) and dropping in UR.SYSTEM as the sole launcher:
#   make apple2-bootdisk PRODOS_DISK="/path/to/ProDOS.dsk"
# Run it on the ENHANCED //e:  mame apple2ee -flop1 build/apple2/ur-boot.po
.PHONY: apple2-bootdisk
apple2-bootdisk: apple2
	AC="$(AC)" tools/apple2-bootdisk.sh "$(PRODOS_DISK)" \
	   $(APPLE2_OUT)/ur.system $(APPLE2_OUT)/ur-boot.po

# Download + unpack the pinned fujinet-lib release for apple2.
$(APPLE2_LIB):
	@mkdir -p $(APPLE2_FNLIB_DIR)
	curl -fsSL $(APPLE2_FNLIB_URL) -o $(APPLE2_FNLIB_DIR)/fujinet-lib.zip
	cd $(APPLE2_FNLIB_DIR) && unzip -o -q fujinet-lib.zip
	@test -f $(APPLE2_LIB) || { echo "ERROR: $(APPLE2_LIB) missing after unzip"; exit 1; }

$(APPLE2_OUT):
	mkdir -p $@

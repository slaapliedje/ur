# Coleco Adam — Z80, built with z88dk (NOT cc65). zcc drives the build.
ADAM_OUT     := $(BUILD_DIR)/adam
ZCC          ?= zcc
ADAM_SOURCES := $(COMMON_SOURCES) $(NET_SOURCES) $(wildcard $(SRC_DIR)/adam/*.c)
# z88dk: the Coleco target with the `adam` subtype (CP/M disk support + adam lib).
# Confirm exact target/subtype/clib against the z88dk Coleco-ADAM platform docs
# before the first real build.
ADAM_FLAGS   := +coleco -subtype=adam -compiler=sdcc -O2 $(COMMON_INC)

.PHONY: adam
adam: | $(ADAM_OUT) ## Build the Coleco Adam target (.dsk, z88dk)
ifeq ($(strip $(ADAM_SOURCES)),)
	@echo "[adam] no sources yet — add src/adam/*.c, then build (z88dk). (skeleton)"
else
	$(ZCC) $(ADAM_FLAGS) $(ADAM_SOURCES) -o $(ADAM_OUT)/ur -create-app
	@echo "TODO: confirm z88dk-appmake output (.dsk/.ddp) for Adam"
endif

$(ADAM_OUT):
	mkdir -p $@

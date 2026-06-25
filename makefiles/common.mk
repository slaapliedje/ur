# Shared build variables (included once by the top-level Makefile).

BUILD_DIR  ?= build
LIB_DIR    ?= lib
SRC_DIR    ?= src

# Pinned fujinet-lib release; downloaded per target into lib/fujinet-lib/<target>/.
FNLIB_VERSION ?= 4.11.2
COMMON_DIR := $(SRC_DIR)/common
NET_DIR    := $(SRC_DIR)/net

# Portable, toolchain-neutral core shared by EVERY target.
COMMON_SOURCES := $(wildcard $(COMMON_DIR)/*.c)

# Networking client code (uses fujinet-lib); compiled per platform.
# NET_SOURCES := $(wildcard $(NET_DIR)/*.c)
NET_SOURCES :=

# Shared include path for the platform interface (src/common/plat.h) etc.
COMMON_INC := -I$(COMMON_DIR) -I$(NET_DIR)

# Auto-enable the c_sp compatibility shim on cc65 <= 2.19, whose runtime exports
# `sp` (current cc65 renamed it to `c_sp`, which fujinet-lib's prebuilt libs need).
# Detected by checking the cc65 runtime's zeropage.inc; override by setting
# CSP_COMPAT yourself. Current cc65 has c_sp, so the shim stays off (and on CI).
ifeq ($(origin CSP_COMPAT),undefined)
  CL65_BIN := $(shell command -v cl65 2>/dev/null)
  CC65_ZP  := $(firstword $(wildcard \
                $(CC65_HOME)/asminc/zeropage.inc \
                $(if $(CL65_BIN),$(abspath $(dir $(CL65_BIN))/../share/cc65)/asminc/zeropage.inc) \
                /usr/share/cc65/asminc/zeropage.inc \
                /usr/local/share/cc65/asminc/zeropage.inc))
  ifneq ($(CC65_ZP),)
    ifeq ($(shell grep -q c_sp $(CC65_ZP) 2>/dev/null && echo y),)
      CSP_COMPAT := 1
    endif
  endif
endif

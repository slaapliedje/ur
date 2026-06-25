# Shared build variables (included once by the top-level Makefile).

BUILD_DIR  ?= build
LIB_DIR    ?= lib
SRC_DIR    ?= src
COMMON_DIR := $(SRC_DIR)/common
NET_DIR    := $(SRC_DIR)/net

# Portable, toolchain-neutral core shared by EVERY target. Populate as code lands.
# COMMON_SOURCES := $(wildcard $(COMMON_DIR)/*.c)
COMMON_SOURCES :=

# Networking client code (uses fujinet-lib); compiled per platform.
# NET_SOURCES := $(wildcard $(NET_DIR)/*.c)
NET_SOURCES :=

# Shared include path for the platform interface (src/common/plat.h) etc.
COMMON_INC := -I$(COMMON_DIR) -I$(NET_DIR)

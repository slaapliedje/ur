# Sega Mega Drive / Genesis — 68000, built with SGDK 2.11 (m68k-elf-gcc + libmd).
# Offline cartridge ROM (no FujiNet). The shared core + controller compile
# unchanged under GCC for the 68000, same as the Atari ST family — this target
# just swaps the OS (none) and the video/sound layer (SGDK's VDP + PSG APIs).
#
# Toolchain (not assumed installed — see docs/development.md):
#   GDK        -> an SGDK 2.11 checkout with lib/libmd.a built for Linux
#                 (git clone -b v2.11 https://github.com/Stephane-D/SGDK;
#                  PATH=<m68k-elf bin>:$PATH make -f makelib.gen release)
#   M68KELF_BIN-> a bare-metal m68k-elf GCC (e.g. marsdev's prebuilt toolchain);
#                 NOT the m68k-atari-mint compiler the ST target uses.
# We replicate SGDK's makefile.gen ROM recipe here (rom_head -> sega.o -> link
# against md.ld + libmd -> objcopy -> pad) instead of adopting its fixed
# src/res/out project layout.
GEN_OUT     := $(BUILD_DIR)/genesis
GDK         ?= $(HOME)/dev/toolchains/sgdk211
M68KELF_BIN ?= $(HOME)/dev/toolchains/mars/m68k-elf/bin
GEN_CC      := $(M68KELF_BIN)/m68k-elf-gcc
GEN_OBJCOPY := $(M68KELF_BIN)/m68k-elf-objcopy

# SGDK's release flags (makefile.gen), minus its project include layout.
GEN_FLAGS   := -DSGDK_GCC -DUR_GENESIS -m68000 -Wall -Wextra -Wno-shift-negative-value \
               -Wno-main -Wno-unused-parameter -fno-builtin -fms-extensions \
               -ffunction-sections -fdata-sections \
               -I$(GDK)/inc -I$(GDK)/res -I$(SRC_DIR)/genesis $(COMMON_INC) \
               -O3 -fuse-linker-plugin -fno-web -fno-gcse -fomit-frame-pointer \
               -flto -flto=auto -ffat-lto-objects
GEN_SOURCES := $(COMMON_SOURCES) $(UR_GAME_SRC) $(wildcard $(SRC_DIR)/genesis/*.c)

.PHONY: genesis megadrive
megadrive: genesis
genesis: | $(GEN_OUT)/out ## Build the Sega Mega Drive / Genesis ROM (SGDK 2.11)
ifeq ($(wildcard $(GDK)/lib/libmd.a),)
	@echo "[genesis] SGDK not found (need $$GDK/lib/libmd.a + m68k-elf gcc in $$M68KELF_BIN) — skipping. See docs/development.md."
else
	$(GEN_CC) $(GEN_FLAGS) -c $(SRC_DIR)/genesis/boot/rom_head.c -o $(GEN_OUT)/rom_head.o
	$(GEN_OBJCOPY) -O binary $(GEN_OUT)/rom_head.o $(GEN_OUT)/out/rom_head.bin
	$(GEN_CC) -x assembler-with-cpp -Wa,--register-prefix-optional,--bitwise-or \
	  -Wa,-I$(GEN_OUT) $(GEN_FLAGS) -c $(GDK)/src/boot/sega.s -o $(GEN_OUT)/sega.o
	$(GEN_CC) $(GEN_FLAGS) -n -T $(GDK)/md.ld -nostdlib -Wl,--gc-sections \
	  $(GEN_OUT)/sega.o $(GEN_SOURCES) $(GDK)/lib/libmd.a -lgcc -o $(GEN_OUT)/rom.out
	$(GEN_OBJCOPY) -O binary $(GEN_OUT)/rom.out $(GEN_OUT)/ur.bin
	java -jar $(GDK)/bin/sizebnd.jar $(GEN_OUT)/ur.bin -sizealign 131072 -checksum
	@echo "[genesis] built $(GEN_OUT)/ur.bin — BlastEm / RetroArch (Genesis Plus GX) / dgen"
endif

$(GEN_OUT)/out:
	mkdir -p $@

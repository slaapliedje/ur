/* Sega Mega Drive / Genesis ROM header for Ur (SGDK's src/boot/rom_head.c with
 * our identity fields; sizebnd fills the checksum at package time). Field
 * widths are fixed — keep every string exactly as long as its array. */
#include "genesis.h"

__attribute__((externally_visible))
const ROMHeader rom_header = {
    "SEGA MEGA DRIVE ",
    "(C)SLPJ 2026.AUG",
    "THE ROYAL GAME OF UR                            ",
    "THE ROYAL GAME OF UR                            ",
    "GM UR000001-00",
    0x000,
    "J               ",
    0x00000000,
    0x000FFFFF,
    0xE0FF0000,
    0xE0FFFFFF,
    "RA",
    0xF820,
    0x00200000,
    0x0020FFFF,
    "            ",
    "THE ROYAL GAME OF UR - GPLV3 OPEN SOURCE",
    "JUE             "
};

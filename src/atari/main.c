/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit "hello, FujiNet" — Phase 1 toolchain bring-up.
 *
 * Not the game yet. This proves two things end to end:
 *   1. the cc65 toolchain builds a bootable Atari program (run it in Altirra), and
 *   2. we can talk to the FujiNet device via fujinet-lib — here we read the
 *      adapter config over SIO and print it.
 *
 * Run with FujiNet-PC (or real FujiNet hardware) attached so the device responds.
 * See docs/development.md and ROADMAP.md (Phase 1).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <conio.h>

#include "fujinet-fuji.h"

int main(void)
{
    AdapterConfig ac;

    clrscr();
    cprintf("The Royal Game of Ur\r\n");
    cprintf("Atari 8-bit  -  FujiNet bring-up\r\n\r\n");

    if (fuji_get_adapter_config(&ac)) {
        cprintf("FujiNet OK\r\n");
        cprintf("fw:   %s\r\n", ac.fn_version);
        cprintf("ssid: %s\r\n", ac.ssid);
        cprintf("ip:   %u.%u.%u.%u\r\n",
                ac.localIP[0], ac.localIP[1], ac.localIP[2], ac.localIP[3]);
    } else {
        cprintf("FujiNet not responding.\r\n");
        cprintf("(Is FujiNet / FujiNet-PC attached?)\r\n");
    }

    cprintf("\r\nPress any key...\r\n");
    cgetc();
    return 0;
}

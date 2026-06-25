/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Coleco Adam "hello, FujiNet" — Phase 1 toolchain bring-up (Z80 / z88dk).
 *
 * The Z80 counterpart of src/atari/main.c. It proves two things end to end:
 *   1. the z88dk toolchain builds an Adam program, and
 *   2. we can reach the FujiNet device (over the AdamNet bus) via fujinet-lib —
 *      here we read the adapter config and print it.
 *
 * Run in MAME (adam driver) or ADAMEm with FujiNet-PC / real FujiNet attached.
 * Uses z88dk's <stdio.h> (not cc65 conio). See docs/development.md, ROADMAP.md.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "fujinet-fuji.h"

int main(void)
{
    AdapterConfig ac;

    printf("The Royal Game of Ur\n");
    printf("Coleco Adam  -  FujiNet bring-up\n\n");

    if (fuji_get_adapter_config(&ac)) {
        printf("FujiNet OK\n");
        printf("fw:   %s\n", ac.fn_version);
        printf("ssid: %s\n", ac.ssid);
        printf("ip:   %u.%u.%u.%u\n",
               ac.localIP[0], ac.localIP[1], ac.localIP[2], ac.localIP[3]);
    } else {
        printf("FujiNet not responding.\n");
        printf("(Is FujiNet / FujiNet-PC attached?)\n");
    }

    printf("\nPress a key...\n");
    getchar();
    return 0;
}

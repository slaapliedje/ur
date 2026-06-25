/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Atari 8-bit "hello, FujiNet" — Phase 1 toolchain + networking bring-up.
 *
 * Not the game yet. It proves the path end to end:
 *   1. the cc65 toolchain builds a bootable Atari program (run it in Altirra),
 *   2. we can reach the FujiNet device via fujinet-lib (reads adapter config), and
 *   3. we can do an N: TCP round-trip through FujiNet/FujiNet-PC (echo test).
 *
 * To exercise the network test, run the echo server on the host first:
 *     python3 tools/echo-server.py            # listens on 0.0.0.0:1234
 * then start FujiNet-PC and boot this in Altirra. Change UR_NET_URL below to
 * match your host/port. See docs/development.md and ROADMAP.md (Phase 1).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <conio.h>

#include "fujinet-fuji.h"
#include "fujinet-network.h"

/* TCP endpoint to echo against. "localhost" resolves on the FujiNet-PC host. */
#define UR_NET_URL "N:TCP://localhost:1234/"

static void show_adapter(void)
{
    AdapterConfig ac;

    if (fuji_get_adapter_config(&ac)) {
        cprintf("FujiNet OK  fw %s\r\n", ac.fn_version);
        cprintf("ip %u.%u.%u.%u\r\n",
                ac.localIP[0], ac.localIP[1], ac.localIP[2], ac.localIP[3]);
    } else {
        cprintf("FujiNet not responding.\r\n");
    }
}

static void net_echo(void)
{
    const char  msg[] = "HELLO UR\n";
    char        buf[64];
    uint16_t    bw = 0;
    uint8_t     conn, err;
    int16_t     n;
    uint8_t     tries;

    cprintf("\r\nnet: %s\r\n", UR_NET_URL);

    if (network_init() != FN_ERR_OK) {
        cprintf("network_init failed\r\n");
        return;
    }
    if ((err = network_open(UR_NET_URL, OPEN_MODE_RW, 0)) != FN_ERR_OK) {
        cprintf("open failed (err %u)\r\n", err);
        return;
    }
    if ((err = network_write(UR_NET_URL, (const uint8_t *)msg, sizeof(msg) - 1))
            != FN_ERR_OK) {
        cprintf("write failed (err %u)\r\n", err);
        network_close(UR_NET_URL);
        return;
    }
    cprintf("sent: %s", msg);

    /* Wait for the echo to land in FujiNet's receive buffer. */
    for (tries = 0; tries < 250 && bw == 0; ++tries)
        network_status(UR_NET_URL, &bw, &conn, &err);

    if (bw == 0) {
        cprintf("no reply\r\n");
    } else {
        if (bw > sizeof(buf) - 1)
            bw = sizeof(buf) - 1;
        n = network_read(UR_NET_URL, (uint8_t *)buf, bw);
        if (n > 0) {
            buf[n] = '\0';
            cprintf("recv: %s\r\n", buf);
        } else {
            cprintf("read failed (%d)\r\n", n);
        }
    }
    network_close(UR_NET_URL);
}

int main(void)
{
    clrscr();
    cprintf("The Royal Game of Ur\r\n");
    cprintf("Atari 8-bit  -  FujiNet bring-up\r\n\r\n");

    show_adapter();
    net_echo();

    cprintf("\r\nPress any key...\r\n");
    cgetc();
    return 0;
}

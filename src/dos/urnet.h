/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * urnet — the DOS port's N: network layer, talking INT F5h directly.
 *
 * fujinet-lib 4.11.2's msdos bus layer predates FUJINET.SYS's field-descriptor
 * convention (DH = FUJI_FIELD_*), so its network calls reach the FujiNet with
 * ZERO aux fields ("Insufficient open paramaters: 0") — only its payload-less
 * and payload-only calls (the appkey set) work. Rather than patch the lib, the
 * network device is driven directly with the canonical register contract from
 * fujinet-msdos (include/fuji_f5.h + ncopy/fujifs.c):
 *
 *   DL = direction (00 none / 40 read / 80 write)   DH = field descriptor
 *   AL = device    AH = command    CX = aux12    SI = aux34
 *   ES:BX = payload buffer    DI = length    -> AL = 'C' ok / 'E' error
 *
 * We use network device 1 (0x71). The appkey calls still go through
 * fujinet-lib (fuji_*_appkey), which speaks the same frames correctly.
 */
#ifndef UR_URNET_H
#define UR_URNET_H

#include <stdint.h>

#define URNET_OK 0

/* mode: 4 = read (HTTP GET), 12 = read/write (TCP). trans: 0 = none. */
uint8_t urnet_open(const char *spec, uint8_t mode, uint8_t trans);
uint8_t urnet_close(void);
uint8_t urnet_status(uint16_t *bw, uint8_t *conn, uint8_t *err);
int16_t urnet_read(uint8_t *buf, uint16_t len);
uint8_t urnet_write(const uint8_t *buf, uint16_t len);

#endif /* UR_URNET_H */

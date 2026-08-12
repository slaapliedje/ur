/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See urnet.h for why this exists. Patterns after fujinet-msdos ncopy/fujifs.c. */
#ifdef UR_ONLINE

#include <string.h>
#include "urnet.h"

/* INT F5 register contract (canonical, from fujinet-msdos include/fuji_f5.h) */
extern int fujiF5w(uint16_t descrdir, uint16_t devcom,
                   uint16_t aux12, uint16_t aux34,
                   void __far *buffer, uint16_t length);
#pragma aux fujiF5w = \
  "int 0xf5" \
  parm [dx] [ax] [cx] [si] [es bx] [di] \
  modify [ax]

#define DIR_NONE  0x00u
#define DIR_READ  0x40u
#define DIR_WRITE 0x80u
#define FIELD_NONE  0u   /* no aux fields in the command frame  */
#define FIELD_A1_A2 2u   /* aux1 + aux2 (open: mode + trans)    */
#define FIELD_B12   5u   /* aux12 as one 16-bit value (lengths) */

#define NETDEV    0x71u  /* network device #1 */
#define CMD_OPEN   'O'
#define CMD_CLOSE  'C'
#define CMD_READ   'R'
#define CMD_WRITE  'W'
#define CMD_STATUS 'S'

#define REPLY_OK  'C'
#define OPEN_SIZE 256    /* devicespec payload is a fixed-size frame */

static int f5(uint8_t dir, uint8_t descr, uint8_t cmd,
              uint16_t aux12, void __far *buf, uint16_t len)
{
    return fujiF5w((uint16_t)((descr << 8) | dir),
                   (uint16_t)((cmd << 8) | NETDEV),
                   aux12, 0, buf, len);
}

uint8_t urnet_open(const char *spec, uint8_t mode, uint8_t trans)
{
    static uint8_t frame[OPEN_SIZE];
    uint16_t i;
    for (i = 0; spec[i] && i < OPEN_SIZE - 1; i++) frame[i] = (uint8_t)spec[i];
    memset(frame + i, 0, (size_t)(OPEN_SIZE - i));
    return f5(DIR_WRITE, FIELD_A1_A2, CMD_OPEN,
              (uint16_t)(mode | ((uint16_t)trans << 8)),
              (void __far *)frame, OPEN_SIZE) == REPLY_OK ? URNET_OK : 1;
}

uint8_t urnet_close(void)
{
    return f5(DIR_NONE, FIELD_NONE, CMD_CLOSE, 0, 0, 0) == REPLY_OK ? URNET_OK : 1;
}

uint8_t urnet_status(uint16_t *bw, uint8_t *conn, uint8_t *err)
{
    static struct { uint16_t length; uint8_t connected; uint8_t errcode; } st;
    if (f5(DIR_READ, FIELD_NONE, CMD_STATUS, 0,
           (void __far *)&st, sizeof(st)) != REPLY_OK)
        return 1;
    *bw = st.length; *conn = st.connected; *err = st.errcode;
    return URNET_OK;
}

int16_t urnet_read(uint8_t *buf, uint16_t len)
{
    if (f5(DIR_READ, FIELD_B12, CMD_READ, len,
           (void __far *)buf, len) != REPLY_OK)
        return -1;
    return (int16_t)len;
}

uint8_t urnet_write(const uint8_t *buf, uint16_t len)
{
    return f5(DIR_WRITE, FIELD_B12, CMD_WRITE, len,
              (void __far *)buf, len) == REPLY_OK ? URNET_OK : 1;
}

#endif /* UR_ONLINE */

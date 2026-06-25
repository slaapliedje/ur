/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ur multiplayer wire-protocol codec. See proto.h and docs/protocol.md. */

#include "proto.h"

uint8_t ur_proto_join(uint8_t *buf)
{
    buf[0] = UR_MSG_JOIN;
    buf[1] = UR_PROTO_VERSION;
    return 2;
}

uint8_t ur_proto_roll(uint8_t *buf)
{
    buf[0] = UR_MSG_ROLL;
    return 1;
}

uint8_t ur_proto_move(uint8_t *buf, uint8_t piece)
{
    buf[0] = UR_MSG_MOVE;
    buf[1] = piece;
    return 2;
}

uint8_t ur_proto_encode_state(uint8_t *buf, const ur_snapshot *snap)
{
    uint8_t i, n = 0;

    buf[n++] = UR_MSG_STATE;
    buf[n++] = snap->seat;
    buf[n++] = snap->state.turn;
    buf[n++] = snap->phase;
    buf[n++] = snap->roll;
    buf[n++] = (uint8_t)(snap->winner < 0 ? 0xFF : (uint8_t)snap->winner);
    buf[n++] = snap->flags;
    for (i = 0; i < UR_PIECES; i++)
        buf[n++] = snap->state.piece[0][i];
    for (i = 0; i < UR_PIECES; i++)
        buf[n++] = snap->state.piece[1][i];
    return n;                       /* == UR_STATE_MSG_LEN */
}

bool ur_proto_decode_state(const uint8_t *buf, uint8_t len, ur_snapshot *snap)
{
    uint8_t i, n;
    uint8_t w;

    if (len < UR_STATE_MSG_LEN || buf[0] != UR_MSG_STATE)
        return false;

    n = 1;
    snap->seat       = buf[n++];
    snap->state.turn = buf[n++];
    snap->phase      = buf[n++];
    snap->roll       = buf[n++];
    w                = buf[n++];
    snap->winner     = (w == 0xFF) ? (int8_t)-1 : (int8_t)w;
    snap->flags      = buf[n++];
    for (i = 0; i < UR_PIECES; i++)
        snap->state.piece[0][i] = buf[n++];
    for (i = 0; i < UR_PIECES; i++)
        snap->state.piece[1][i] = buf[n++];
    return true;
}

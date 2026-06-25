/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * proto.h — the Ur multiplayer wire protocol (the cross-platform contract).
 *
 * Compact, fixed-size, byte-oriented messages so they're trivial to parse on a
 * 6502 or Z80. Server-authoritative: the client sends intents (join/roll/move)
 * and renders the STATE snapshots the server sends. Toolchain-neutral C — see
 * docs/protocol.md, src/common/CLAUDE.md, src/net/CLAUDE.md.
 */
#ifndef UR_PROTO_H
#define UR_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "ur.h"

#define UR_PROTO_VERSION 1

/* Message type bytes (byte 0 of every message). */
#define UR_MSG_JOIN   0x01   /* client->server: join          (byte1 = version)     */
#define UR_MSG_ROLL   0x02   /* client->server: request a roll                       */
#define UR_MSG_MOVE   0x03   /* client->server: move a piece  (byte1 = piece index) */
#define UR_MSG_STATE  0x81   /* server->client: full state snapshot                 */

/* Game phase carried in a STATE snapshot. */
#define UR_PHASE_ROLL 0      /* the current player must roll       */
#define UR_PHASE_MOVE 1      /* the current player must pick a move */
#define UR_PHASE_OVER 2      /* the game is finished               */

/* Event flags from the last applied move (in a STATE snapshot). */
#define UR_FLAG_CAPTURED 0x01
#define UR_FLAG_ROSETTE  0x02
#define UR_FLAG_SCORED   0x04

/* On-the-wire length of a STATE message. */
#define UR_STATE_MSG_LEN 21

/* A decoded server STATE snapshot. */
typedef struct {
    uint8_t  seat;     /* which player THIS client controls (0/1)            */
    uint8_t  phase;    /* UR_PHASE_*                                         */
    uint8_t  roll;     /* 0..4, or 0xFF when there is no current roll        */
    int8_t   winner;   /* 0/1, or -1 if the game is not over                 */
    uint8_t  flags;    /* UR_FLAG_* from the last move                       */
    ur_state state;    /* board positions + whose turn                       */
} ur_snapshot;

/* Client->server encoders. Each returns the number of bytes written to buf. */
uint8_t ur_proto_join(uint8_t *buf);
uint8_t ur_proto_roll(uint8_t *buf);
uint8_t ur_proto_move(uint8_t *buf, uint8_t piece);

/* STATE encode/decode. encode returns bytes written (UR_STATE_MSG_LEN);
 * decode returns false if the buffer is too short or not a STATE message. */
uint8_t ur_proto_encode_state(uint8_t *buf, const ur_snapshot *snap);
bool    ur_proto_decode_state(const uint8_t *buf, uint8_t len, ur_snapshot *snap);

#endif /* UR_PROTO_H */

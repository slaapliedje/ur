/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ur.h — the portable Royal Game of Ur rules engine.
 *
 * Pure, deterministic, toolchain-neutral C (compiles under cc65, z88dk/SDCC, and
 * the host compiler). No platform headers, no hardware, no I/O. See
 * src/common/CLAUDE.md, docs/rules.md, and docs/architecture.md.
 *
 * Path model (Finkel ruleset): each player's piece travels positions
 *   0  = start  (off board, not yet entered)
 *   1..4   = private entry block
 *   5..12  = shared middle row  (the only capture zone)
 *   13..14 = private exit block
 *   15 = home  (borne off; UR_POS_HOME)
 * Rosettes (extra roll + safe from capture) are at path steps 4, 8 and 14.
 * Bear-off needs an exact roll to land on 15.
 */
#ifndef UR_H
#define UR_H

#include <stdint.h>
#include <stdbool.h>

#define UR_PIECES        7
#define UR_PATH_LEN      14
#define UR_POS_START     0
#define UR_POS_HOME      (UR_PATH_LEN + 1)   /* 15 */
#define UR_NUM_PLAYERS   2

/* Inclusive range of path positions that lie on the shared middle row. */
#define UR_SHARED_FIRST  5
#define UR_SHARED_LAST   12

typedef struct {
    uint8_t piece[UR_NUM_PLAYERS][UR_PIECES]; /* position 0..15 of each piece */
    uint8_t turn;                             /* whose turn: 0 or 1 */
} ur_state;

typedef struct {
    bool captured;   /* an opponent piece was sent back to start */
    bool scored;     /* this piece was borne off (reached home) */
    bool rosette;    /* landed on a rosette -> same player rolls again */
    bool won;        /* this move completed the game */
} ur_move_result;

/* ---- setup -------------------------------------------------------------- */
void     ur_init(ur_state *s);

/* ---- board predicates --------------------------------------------------- */
bool     ur_is_rosette(uint8_t pos);   /* true for path positions 4, 8, 14 */
bool     ur_is_shared(uint8_t pos);    /* true for the shared middle row */
/* True if landing on `dest` would capture an opponent (shared row, not a rosette,
 * opponent present) — for the ports' move-list "capture" marker. */
bool     ur_dest_captures(const ur_state *s, uint8_t player, uint8_t dest);

/* ---- dice / RNG (deterministic; seed comes from the platform) ----------- */
void     ur_rng_seed(uint16_t seed);
uint16_t ur_rand(void);
uint8_t  ur_dice_roll(void);           /* four binary dice -> 0..4 */

/* ---- moves -------------------------------------------------------------- */
bool     ur_move_legal(const ur_state *s, uint8_t player, uint8_t piece, uint8_t roll);
/* Fills pieces_out (may be NULL) with the indices of movable pieces; returns count. */
uint8_t  ur_legal_moves(const ur_state *s, uint8_t player, uint8_t roll, uint8_t *pieces_out);
/* Applies a (legal) move; fills res (may be NULL). Returns false if illegal. */
bool     ur_apply_move(ur_state *s, uint8_t player, uint8_t piece, uint8_t roll,
                       ur_move_result *res);
/* Flip to the other player unless the last move earned an extra roll / won. */
void     ur_advance_turn(ur_state *s, const ur_move_result *res);

/* ---- AI opponent -------------------------------------------------------- */
/* Difficulty levels for ur_ai_pick(). */
#define UR_AI_EASY    0   /* a random legal move (a gentle, beatable opponent)   */
#define UR_AI_NORMAL  1   /* 1-ply greedy on the positional eval (+ rosette)     */
#define UR_AI_HARD    2   /* greedy + capture/rosette/bear-off bonuses & safety  */

/* Pick the best legal piece for `player` to move given `roll`, at difficulty
 * `level` (UR_AI_*). Returns a piece index 0..UR_PIECES-1, or -1 if no legal move. */
int8_t   ur_ai_pick(const ur_state *s, uint8_t player, uint8_t roll, uint8_t level);

/* ---- status ------------------------------------------------------------- */
uint8_t  ur_score(const ur_state *s, uint8_t player);  /* pieces home (0..7) */
int8_t   ur_winner(const ur_state *s);                 /* 0, 1, or -1 (none) */

#endif /* UR_H */

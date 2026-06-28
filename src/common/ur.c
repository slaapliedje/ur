/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Portable Royal Game of Ur rules engine. See ur.h. */

#include "ur.h"

/* ---- board predicates --------------------------------------------------- */

bool ur_is_rosette(uint8_t pos)
{
    return pos == 4 || pos == 8 || pos == 14;
}

bool ur_is_shared(uint8_t pos)
{
    return pos >= UR_SHARED_FIRST && pos <= UR_SHARED_LAST;
}

/* ---- occupancy helpers -------------------------------------------------- */

static bool own_occupies(const ur_state *s, uint8_t player, uint8_t pos)
{
    uint8_t i;
    for (i = 0; i < UR_PIECES; i++)
        if (s->piece[player][i] == pos)
            return true;
    return false;
}

/* Index of `opp`'s piece sitting on path position `pos`, or -1. Only meaningful
 * on shared squares, where both players' equal path indices are the same tile. */
static int8_t opp_piece_at(const ur_state *s, uint8_t opp, uint8_t pos)
{
    uint8_t i;
    for (i = 0; i < UR_PIECES; i++)
        if (s->piece[opp][i] == pos)
            return (int8_t)i;
    return -1;
}

/* ---- setup -------------------------------------------------------------- */

void ur_init(ur_state *s)
{
    uint8_t pl, i;
    for (pl = 0; pl < UR_NUM_PLAYERS; pl++)
        for (i = 0; i < UR_PIECES; i++)
            s->piece[pl][i] = UR_POS_START;
    s->turn = 0;
}

/* ---- dice / RNG --------------------------------------------------------- */
/* 16-bit xorshift: identical sequence on host, 6502 (cc65) and Z80 (z88dk).
 * Casts force 16-bit wraparound regardless of the platform's int width. */

static uint16_t ur_rng_state = 1;

void ur_rng_seed(uint16_t seed)
{
    ur_rng_state = seed ? seed : 1u;
}

uint16_t ur_rand(void)
{
    uint16_t x = ur_rng_state;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    ur_rng_state = x;
    return x;
}

uint8_t ur_dice_roll(void)
{
    /* Four binary tetrahedral dice = four coin flips; distribution 1/4/6/4/1. */
    uint8_t i, n = 0;
    for (i = 0; i < 4; i++)
        n += (uint8_t)((ur_rand() >> 15) & 1u);   /* top bit: best quality */
    return n;
}

/* ---- moves -------------------------------------------------------------- */

bool ur_move_legal(const ur_state *s, uint8_t player, uint8_t piece, uint8_t roll)
{
    uint8_t p, dest, opp;

    if (roll == 0 || piece >= UR_PIECES)
        return false;

    p = s->piece[player][piece];
    if (p == UR_POS_HOME)
        return false;                       /* already borne off */

    dest = (uint8_t)(p + roll);
    if (dest > UR_POS_HOME)
        return false;                       /* must land exactly on home */
    if (dest == UR_POS_HOME)
        return true;                        /* exact bear-off */

    /* dest is a board square 1..14 */
    if (own_occupies(s, player, dest))
        return false;                       /* cannot stack on own piece */

    if (ur_is_shared(dest)) {
        opp = (uint8_t)(1 - player);
        if (opp_piece_at(s, opp, dest) >= 0)
            return !ur_is_rosette(dest);     /* capture, unless safe rosette */
    }
    return true;
}

uint8_t ur_legal_moves(const ur_state *s, uint8_t player, uint8_t roll, uint8_t *pieces_out)
{
    uint8_t i, n = 0;
    for (i = 0; i < UR_PIECES; i++) {
        if (ur_move_legal(s, player, i, roll)) {
            if (pieces_out)
                pieces_out[n] = i;
            n++;
        }
    }
    return n;
}

bool ur_apply_move(ur_state *s, uint8_t player, uint8_t piece, uint8_t roll,
                   ur_move_result *res)
{
    uint8_t p, dest, opp;
    int8_t  victim;

    if (res) {
        res->captured = false;
        res->scored   = false;
        res->rosette  = false;
        res->won      = false;
    }

    if (!ur_move_legal(s, player, piece, roll))
        return false;

    p    = s->piece[player][piece];
    dest = (uint8_t)(p + roll);

    if (dest != UR_POS_HOME && ur_is_shared(dest)) {
        opp = (uint8_t)(1 - player);
        victim = opp_piece_at(s, opp, dest);   /* legality ensured: not a rosette */
        if (victim >= 0) {
            s->piece[opp][(uint8_t)victim] = UR_POS_START;
            if (res) res->captured = true;
        }
    }

    s->piece[player][piece] = dest;

    if (dest == UR_POS_HOME) {
        if (res) res->scored = true;
    } else if (ur_is_rosette(dest)) {
        if (res) res->rosette = true;
    }

    if (ur_score(s, player) == UR_PIECES && res)
        res->won = true;

    return true;
}

void ur_advance_turn(ur_state *s, const ur_move_result *res)
{
    if (res && (res->rosette || res->won))
        return;                                  /* extra roll / game over */
    s->turn = (uint8_t)(1 - s->turn);
}

/* ---- AI opponent -------------------------------------------------------- */
/* A positional heuristic: advance pieces, prize rosettes (safe + extra roll),
 * home pieces, and capturing (which drops the opponent's value); penalise
 * sitting exposed on the shared row. The AI picks the move that maximises
 * (my value - their value) of the resulting position. Deterministic. */

static int16_t piece_value(uint8_t pos)
{
    int16_t v;
    if (pos == UR_POS_START) return 0;
    if (pos == UR_POS_HOME)  return 100;
    v = (int16_t)pos;                  /* progress along the 14-step path */
    if (ur_is_rosette(pos))
        v += 6;                        /* safe tile + earns an extra roll */
    else if (ur_is_shared(pos))
        v -= 3;                        /* exposed to capture */
    return v;
}

static int16_t ur_eval(const ur_state *s, uint8_t player)
{
    uint8_t i, opp = (uint8_t)(1 - player);
    int16_t me = 0, them = 0;
    for (i = 0; i < UR_PIECES; i++) {
        me   += piece_value(s->piece[player][i]);
        them += piece_value(s->piece[opp][i]);
    }
    return (int16_t)(me - them);
}

/* Expected loss if our piece sits exposed at shared-lane square `dest`: how likely
 * an opponent piece behind it captures next turn (weighted by the 1/4/6/4/1 roll
 * distribution), times what the piece is worth. Rosette squares are safe. Used by
 * the Hard AI to avoid leaving pieces in harm's way. */
static int16_t ur_capture_risk(const ur_state *s, uint8_t player, uint8_t dest)
{
    static const uint8_t prob[5] = { 0, 4, 6, 4, 1 };   /* ways to roll 1..4 of 16 */
    uint8_t opp = (uint8_t)(1 - player), i, oppos;
    uint16_t risk = 0;

    if (dest < 5 || dest > 12) return 0;     /* only the shared middle row is contested */
    if (ur_is_rosette(dest))   return 0;     /* the shared rosette is safe */
    for (i = 0; i < UR_PIECES; i++) {
        oppos = s->piece[opp][i];
        if (oppos >= 5 && oppos < dest && (uint8_t)(dest - oppos) <= 4)
            risk += prob[dest - oppos];
    }
    return (int16_t)((risk * (uint16_t)piece_value(dest)) / 16u);
}

int8_t ur_ai_pick(const ur_state *s, uint8_t player, uint8_t roll, uint8_t level)
{
    uint8_t pieces[UR_PIECES];
    uint8_t count, i;
    int8_t  best = -1;
    int16_t best_score = -30000;

    count = ur_legal_moves(s, player, roll, pieces);
    if (count == 0)
        return -1;

    /* Easy: just play a random legal move. */
    if (level == UR_AI_EASY)
        return (int8_t)pieces[ur_rand() % count];

    for (i = 0; i < count; i++) {
        ur_state tmp;            /* cc65 won't aggregate-init from a runtime value */
        ur_move_result r;
        int16_t score;
        tmp = *s;                /* ...so copy by assignment */
        ur_apply_move(&tmp, player, pieces[i], roll, &r);
        score = ur_eval(&tmp, player);
        if (r.rosette)
            score += (level == UR_AI_HARD) ? 8 : 4;   /* the extra roll is valuable */
        if (level == UR_AI_HARD) {
            if (r.captured) score += 15;              /* press the attack */
            if (r.scored)   score += 10;              /* bear a piece off */
            /* avoid ending exposed in the shared lane */
            score -= ur_capture_risk(s, player,
                         (uint8_t)(s->piece[player][pieces[i]] + roll));
        }
        if (score > best_score) {
            best_score = score;
            best = (int8_t)pieces[i];
        }
    }
    return best;
}

/* ---- status ------------------------------------------------------------- */

uint8_t ur_score(const ur_state *s, uint8_t player)
{
    uint8_t i, n = 0;
    for (i = 0; i < UR_PIECES; i++)
        if (s->piece[player][i] == UR_POS_HOME)
            n++;
    return n;
}

int8_t ur_winner(const ur_state *s)
{
    if (ur_score(s, 0) == UR_PIECES) return 0;
    if (ur_score(s, 1) == UR_PIECES) return 1;
    return -1;
}

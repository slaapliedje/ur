/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host unit tests for the portable rules engine (src/common/ur.c). */

#include <stdio.h>
#include <stdint.h>
#include "ur.h"
#include "proto.h"

static int checks = 0, fails = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            fails++;                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

static void test_init(void)
{
    ur_state s;
    uint8_t i;
    ur_init(&s);
    for (i = 0; i < UR_PIECES; i++) {
        CHECK(s.piece[0][i] == UR_POS_START);
        CHECK(s.piece[1][i] == UR_POS_START);
    }
    CHECK(s.turn == 0);
    CHECK(ur_score(&s, 0) == 0);
    CHECK(ur_winner(&s) == -1);
}

static void test_predicates(void)
{
    CHECK(ur_is_rosette(4));
    CHECK(ur_is_rosette(8));
    CHECK(ur_is_rosette(14));
    CHECK(!ur_is_rosette(1));
    CHECK(!ur_is_rosette(15));
    CHECK(ur_is_shared(5));
    CHECK(ur_is_shared(12));
    CHECK(!ur_is_shared(4));   /* private entry */
    CHECK(!ur_is_shared(13));  /* private exit */
}

static void test_move_gen(void)
{
    ur_state s;
    ur_init(&s);
    CHECK(ur_legal_moves(&s, 0, 0, NULL) == 0);   /* roll of 0: no moves */
    CHECK(ur_legal_moves(&s, 0, 2, NULL) == 7);   /* all pieces may enter */
    CHECK(ur_legal_moves(&s, 0, 4, NULL) == 7);
}

static void test_bear_off(void)
{
    ur_state s;
    ur_move_result r;
    ur_init(&s);
    s.piece[0][0] = 14;
    CHECK(ur_move_legal(&s, 0, 0, 1));    /* 14+1=15 home, exact */
    CHECK(!ur_move_legal(&s, 0, 0, 2));   /* 16 overshoots */
    CHECK(ur_apply_move(&s, 0, 0, 1, &r));
    CHECK(r.scored);
    CHECK(!r.rosette);
    CHECK(s.piece[0][0] == UR_POS_HOME);
}

static void test_capture(void)
{
    ur_state s;
    ur_move_result r;
    ur_init(&s);
    s.piece[0][0] = 5;
    s.piece[1][0] = 7;                    /* opponent on a shared, non-rosette tile */
    CHECK(ur_move_legal(&s, 0, 0, 2));    /* 5+2=7 -> capture */
    CHECK(ur_apply_move(&s, 0, 0, 2, &r));
    CHECK(r.captured);
    CHECK(s.piece[0][0] == 7);
    CHECK(s.piece[1][0] == UR_POS_START); /* victim sent back to start */
}

static void test_rosette_safe(void)
{
    ur_state s;
    ur_init(&s);
    s.piece[0][0] = 6;
    s.piece[1][0] = 8;                    /* opponent on the central rosette */
    CHECK(!ur_move_legal(&s, 0, 0, 2));   /* 6+2=8 is safe -> cannot land */
}

static void test_rosette_extra(void)
{
    ur_state s;
    ur_move_result r;
    ur_init(&s);
    s.piece[0][0] = 2;
    CHECK(ur_apply_move(&s, 0, 0, 2, &r)); /* 2+2=4 rosette */
    CHECK(r.rosette);
    CHECK(!r.captured);
    CHECK(s.piece[0][0] == 4);
    /* extra roll: turn must NOT advance */
    s.turn = 0;
    ur_advance_turn(&s, &r);
    CHECK(s.turn == 0);
}

static void test_own_block(void)
{
    ur_state s;
    ur_init(&s);
    s.piece[0][0] = 5;
    s.piece[0][1] = 3;
    CHECK(!ur_move_legal(&s, 0, 1, 2));   /* 3+2=5 occupied by own piece */
}

static void test_win(void)
{
    ur_state s;
    ur_move_result r;
    uint8_t i;
    ur_init(&s);
    for (i = 0; i < UR_PIECES - 1; i++)
        s.piece[0][i] = UR_POS_HOME;
    s.piece[0][UR_PIECES - 1] = 14;
    CHECK(ur_score(&s, 0) == 6);
    CHECK(ur_apply_move(&s, 0, UR_PIECES - 1, 1, &r));
    CHECK(r.scored);
    CHECK(r.won);
    CHECK(ur_score(&s, 0) == 7);
    CHECK(ur_winner(&s) == 0);
}

static void test_turn_advance(void)
{
    ur_state s;
    ur_move_result r;
    ur_init(&s);
    /* a plain move (no rosette/win) flips the turn */
    r.captured = r.scored = r.rosette = r.won = false;
    s.turn = 0;
    ur_advance_turn(&s, &r);
    CHECK(s.turn == 1);
    ur_advance_turn(&s, &r);
    CHECK(s.turn == 0);
}

static void test_dice(void)
{
    uint32_t cnt[5] = {0, 0, 0, 0, 0};
    uint32_t sum = 0, i;
    const uint32_t N = 20000;
    uint8_t roll;

    ur_rng_seed(0xBEEF);
    for (i = 0; i < N; i++) {
        roll = ur_dice_roll();
        CHECK(roll <= 4);
        cnt[roll]++;
        sum += roll;
    }
    /* every outcome appears, 2 is the mode, mean ~= 2.0 */
    CHECK(cnt[0] && cnt[1] && cnt[2] && cnt[3] && cnt[4]);
    CHECK(cnt[2] > cnt[0] && cnt[2] > cnt[4]);
    CHECK(sum * 10 > N * 18 && sum * 10 < N * 22);   /* 1.8 < mean < 2.2 */

    /* seeding is reproducible */
    {
        uint16_t a, b;
        ur_rng_seed(123); a = ur_rand();
        ur_rng_seed(123); b = ur_rand();
        CHECK(a == b);
    }
}

static void test_ai_no_move(void)
{
    ur_state s;
    ur_init(&s);
    CHECK(ur_ai_pick(&s, 0, 0, UR_AI_NORMAL) == -1);   /* roll of 0 -> no move */
}

static void test_ai_prefers_capture(void)
{
    ur_state s;
    ur_move_result r;
    int8_t pick;
    ur_init(&s);
    s.piece[0][0] = 5;
    s.piece[1][0] = 7;                   /* capturable opponent at 5+2 */
    pick = ur_ai_pick(&s, 0, 2, UR_AI_NORMAL);
    CHECK(pick >= 0);
    CHECK(ur_apply_move(&s, 0, (uint8_t)pick, 2, &r));
    CHECK(r.captured);                   /* AI takes the capture */
}

static void test_ai_prefers_bear_off(void)
{
    ur_state s;
    ur_move_result r;
    int8_t pick;
    ur_init(&s);
    s.piece[0][0] = 14;                  /* one roll from home */
    pick = ur_ai_pick(&s, 0, 1, UR_AI_NORMAL);
    CHECK(pick >= 0);
    CHECK(ur_apply_move(&s, 0, (uint8_t)pick, 1, &r));
    CHECK(r.scored);
}

static void test_ai_prefers_rosette(void)
{
    ur_state s;
    ur_move_result r;
    int8_t pick;
    ur_init(&s);
    s.piece[0][0] = 2;                   /* 2+2 = 4 rosette */
    s.piece[0][1] = 9;                   /* 9+2 = 11 plain shared */
    pick = ur_ai_pick(&s, 0, 2, UR_AI_NORMAL);
    CHECK(pick >= 0);
    CHECK(ur_apply_move(&s, 0, (uint8_t)pick, 2, &r));
    CHECK(r.rosette);
}

static void test_ai_always_legal(void)
{
    ur_state s;
    int8_t pick;
    uint8_t roll;
    ur_init(&s);
    s.piece[0][0] = 3; s.piece[0][1] = 8;
    s.piece[1][0] = 6; s.piece[1][1] = 11;
    for (roll = 1; roll <= 4; roll++) {
        pick = ur_ai_pick(&s, 0, roll, UR_AI_NORMAL);
        if (pick >= 0)
            CHECK(ur_move_legal(&s, 0, (uint8_t)pick, roll));
    }
}

static void test_ai_selfplay(void)
{
    ur_state s;
    ur_move_result r;
    int8_t pick;
    uint8_t roll;
    uint16_t plies = 0;

    ur_rng_seed(0xABCD);
    ur_init(&s);
    while (ur_winner(&s) < 0 && plies < 5000) {
        roll = ur_dice_roll();
        pick = ur_ai_pick(&s, s.turn, roll, UR_AI_NORMAL);
        if (pick < 0) {
            ur_advance_turn(&s, (const ur_move_result *)0);
        } else {
            ur_apply_move(&s, s.turn, (uint8_t)pick, roll, &r);
            if (!r.won)
                ur_advance_turn(&s, &r);
        }
        plies++;
    }
    CHECK(ur_winner(&s) >= 0);   /* AI vs AI runs to a real finish */
    CHECK(plies < 5000);
}

/* Run one full game with player 0 at level `lvl0` and player 1 at `lvl1`; return
 * the winner. */
static uint8_t ai_match(uint8_t lvl0, uint8_t lvl1, uint16_t seed)
{
    ur_state s;
    ur_move_result r;
    int8_t pick;
    uint8_t roll;
    uint16_t plies = 0;

    ur_rng_seed(seed);
    ur_init(&s);
    while (ur_winner(&s) < 0 && plies < 5000) {
        roll = ur_dice_roll();
        pick = ur_ai_pick(&s, s.turn, roll, s.turn == 0 ? lvl0 : lvl1);
        if (pick < 0) {
            ur_advance_turn(&s, (const ur_move_result *)0);
        } else {
            ur_apply_move(&s, s.turn, (uint8_t)pick, roll, &r);
            if (!r.won)
                ur_advance_turn(&s, &r);
        }
        plies++;
    }
    return (uint8_t)ur_winner(&s);
}

/* The difficulty levels must actually differ in strength: a Hard AI should beat an
 * Easy (random) AI a clear majority of the time. Ur is luck-heavy, so we only
 * require a solid edge, not dominance. */
static void test_ai_levels_strength(void)
{
    uint16_t g, hard_wins = 0, normal_wins = 0;
    for (g = 0; g < 50; g++) {
        if (ai_match(UR_AI_HARD,   UR_AI_EASY, (uint16_t)(0x1100 + g * 7)) == 0) hard_wins++;
        if (ai_match(UR_AI_NORMAL, UR_AI_EASY, (uint16_t)(0x2200 + g * 7)) == 0) normal_wins++;
    }
    CHECK(hard_wins   >= 33);   /* Hard beats random >= ~66% of 50 games   */
    CHECK(normal_wins >= 30);   /* Normal beats random too (a bit weaker)  */
}

static void test_proto_state_roundtrip(void)
{
    ur_snapshot a, b;
    uint8_t buf[32];
    uint8_t n, i;

    ur_init(&a.state);
    a.state.piece[0][0] = 5;
    a.state.piece[1][3] = 8;
    a.state.turn = 1;
    a.seat = 1;
    a.phase = UR_PHASE_MOVE;
    a.roll = 3;
    a.winner = -1;
    a.flags = UR_FLAG_CAPTURED;

    n = ur_proto_encode_state(buf, &a);
    CHECK(n == UR_STATE_MSG_LEN);
    CHECK(ur_proto_decode_state(buf, n, &b));
    CHECK(b.seat == 1 && b.state.turn == 1 && b.phase == UR_PHASE_MOVE);
    CHECK(b.roll == 3 && b.winner == -1 && b.flags == UR_FLAG_CAPTURED);
    for (i = 0; i < UR_PIECES; i++) {
        CHECK(b.state.piece[0][i] == a.state.piece[0][i]);
        CHECK(b.state.piece[1][i] == a.state.piece[1][i]);
    }
}

static void test_proto_winner(void)
{
    ur_snapshot a, b;
    uint8_t buf[32];

    ur_init(&a.state);
    a.seat = 0; a.phase = UR_PHASE_OVER; a.roll = 0xFF; a.winner = 0; a.flags = 0;
    ur_proto_encode_state(buf, &a);
    CHECK(ur_proto_decode_state(buf, UR_STATE_MSG_LEN, &b));
    CHECK(b.winner == 0 && b.phase == UR_PHASE_OVER && b.roll == 0xFF);
}

static void test_proto_commands(void)
{
    uint8_t buf[16];
    CHECK(ur_proto_join(buf, "ABC") == 2 + UR_NAME_LEN && buf[0] == UR_MSG_JOIN && buf[1] == UR_PROTO_VERSION);
    CHECK(buf[2] == 'A' && buf[3] == 'B' && buf[4] == 'C' && buf[5] == ' ');      /* space-padded */
    CHECK(ur_proto_join(buf, "AB3Z") == 2 + UR_NAME_LEN && buf[4] == '3' && buf[5] == 'Z');  /* digits ok */
    CHECK(ur_proto_join(buf, 0) == 2 + UR_NAME_LEN && buf[2] == ' ');
    CHECK(ur_proto_roll(buf) == 1 && buf[0] == UR_MSG_ROLL);
    CHECK(ur_proto_move(buf, 5) == 2 && buf[0] == UR_MSG_MOVE && buf[1] == 5);
}

static void test_proto_truncated(void)
{
    ur_snapshot b;
    uint8_t buf[4] = { UR_MSG_STATE, 0, 0, 0 };
    CHECK(!ur_proto_decode_state(buf, 4, &b));   /* too short -> rejected */
}

int main(void)
{
    test_init();
    test_predicates();
    test_move_gen();
    test_bear_off();
    test_capture();
    test_rosette_safe();
    test_rosette_extra();
    test_own_block();
    test_win();
    test_turn_advance();
    test_dice();
    test_ai_no_move();
    test_ai_prefers_capture();
    test_ai_prefers_bear_off();
    test_ai_prefers_rosette();
    test_ai_always_legal();
    test_ai_selfplay();
    test_ai_levels_strength();
    test_proto_state_roundtrip();
    test_proto_winner();
    test_proto_commands();
    test_proto_truncated();

    printf("%d/%d checks passed\n", checks - fails, checks);
    if (fails) {
        printf("FAILED (%d)\n", fails);
        return 1;
    }
    printf("OK\n");
    return 0;
}

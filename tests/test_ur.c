/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host unit tests for the portable rules engine (src/common/ur.c). */

#include <stdio.h>
#include <stdint.h>
#include "ur.h"

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

    printf("%d/%d checks passed\n", checks - fails, checks);
    if (fails) {
        printf("FAILED (%d)\n", fails);
        return 1;
    }
    printf("OK\n");
    return 0;
}

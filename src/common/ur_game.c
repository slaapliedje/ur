/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ur_game.c — the shared local-game controller (see ur_game.h).
 *
 * This is the turn loop every port used to copy. It calls the plat.h interface for
 * everything machine-specific (draw, wait, choose, sound, RNG entropy) and the
 * src/common rules core for the game logic, so the flow is identical and fixed in
 * one place. Status strings are short and button-neutral (they appear on screens as
 * narrow as ~16 columns). They are UPPERCASE and use only A-Z/0-9/space/!  — the
 * lowest-common-denominator character set across the ports' fonts (the NES font is
 * uppercase-only with no apostrophe), so they render on every target.
 */
#include "ur_game.h"

ur_state ur_g;

static uint8_t g_level = UR_AI_NORMAL;   /* AI difficulty for the current vs-AI game */

static const char M_ROLL[]   = "ROLL THE DICE";
static const char M_CPU[]    = "COMPUTER TURN";
static const char M_NOMOVE[] = "NO LEGAL MOVE";
static const char M_CPUNM[]  = "COMPUTER NO MOVE";
static const char M_CAP[]    = "CAPTURE!";
static const char M_ROS[]    = "ROSETTE AGAIN!";

/* Append the decimal of v (0..99) to p; return the new end. */
static char *fmt_u(char *p, uint8_t v)
{
    if (v >= 10) *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* Build a compact "what the computer just did" line, e.g. "CPU 5>9!" (capture),
 * "CPU 5>9*" (rosette), "CPU 5>H" (bore off), "CPU IN>4" (entered). Uppercase +
 * the lowest-common-denominator charset so it renders on every port (incl. NES). */
static char g_cpu_msg[16];
static void build_cpu_msg(uint8_t src, uint8_t dest, const ur_move_result *res)
{
    char *p = g_cpu_msg;
    *p++ = 'C'; *p++ = 'P'; *p++ = 'U'; *p++ = ' ';
    if (src == UR_POS_START) { *p++ = 'I'; *p++ = 'N'; }
    else                     p = fmt_u(p, src);
    *p++ = '>';
    if (dest >= UR_POS_HOME) *p++ = 'H';          /* bore the piece off */
    else                     p = fmt_u(p, dest);
    if (res->captured)       *p++ = '!';          /* sent an opponent home */
    else if (res->rosette)   *p++ = '*';          /* landed on a rosette   */
    *p = 0;
}

/* A human turn: roll, pick a move, apply it. Returns true if this move won. */
static uint8_t human_turn(uint8_t player)
{
    uint8_t roll, src;
    int8_t  picked;
    ur_move_result res;

    plat_draw(UR_NO_ROLL, M_ROLL);
    plat_wait();
    roll = ur_dice_roll();
    plat_roll(roll);
    plat_draw(roll, (const char *)0);            /* show the roll, then the moves */

    picked = plat_choose_move(player, roll);
    if (picked < 0) {
        plat_draw(roll, M_NOMOVE);
        plat_wait();
        ur_advance_turn(&ur_g, (const ur_move_result *)0);
        return 0;
    }
    src = ur_g.piece[player][(uint8_t)picked];
    ur_apply_move(&ur_g, player, (uint8_t)picked, roll, &res);
    plat_animate(player, src, (uint8_t)(src + roll));
    plat_sfx_result(&res);
    if (res.won)
        return 1;
    if (res.captured || res.rosette) {
        plat_draw(roll, res.captured ? M_CAP : M_ROS);
        plat_wait();
    }
    ur_advance_turn(&ur_g, &res);
    return 0;
}

/* The computer's turn (AI plays Dark). Returns true if this move won. */
static uint8_t computer_turn(uint8_t player)
{
    uint8_t pieces[UR_PIECES], roll, src;
    int8_t  pick;
    ur_move_result res;

    plat_draw(UR_NO_ROLL, M_CPU);
    plat_wait();
    roll = ur_dice_roll();
    plat_roll(roll);

    if (ur_legal_moves(&ur_g, player, roll, pieces) == 0) {
        plat_draw(roll, M_CPUNM);
        plat_wait();
        ur_advance_turn(&ur_g, (const ur_move_result *)0);
        return 0;
    }
    pick = ur_ai_pick(&ur_g, player, roll, g_level);
    src = ur_g.piece[player][(uint8_t)pick];
    ur_apply_move(&ur_g, player, (uint8_t)pick, roll, &res);
    plat_animate(player, src, (uint8_t)(src + roll));
    plat_sfx_result(&res);
    build_cpu_msg(src, (uint8_t)(src + roll), &res);   /* "CPU 5>9!" — show its move */
    plat_draw(roll, g_cpu_msg);
    plat_wait();
    if (res.won)
        return 1;
    ur_advance_turn(&ur_g, &res);
    return 0;
}

uint8_t ur_run_game(uint8_t vs_ai)
{
    static uint8_t seeded = 0;
    uint8_t player;

    if (!seeded) { ur_rng_seed((uint16_t)(plat_seed() | 1u)); seeded = 1; }
    if (vs_ai) g_level = plat_pick_level();   /* Easy / Normal / Hard */

    ur_init(&ur_g);
    for (;;) {
        player = ur_g.turn;
        if (player == 1 && vs_ai) { if (computer_turn(player)) break; }
        else                      { if (human_turn(player))   break; }
    }
    return player;          /* the player who made the winning move */
}

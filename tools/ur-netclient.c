/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ur-netclient — a host test client for the Ur game server.
 *
 * It speaks the REAL wire protocol using the shared client codec
 * (src/common/proto.c) and rules core (src/common/ur.c) — exactly the bytes the
 * Atari/Adam/C64/Apple II ROMs send — but over a plain POSIX TCP socket instead of
 * FujiNet's N: device.  (FujiNet is just a transparent N:TCP pipe to the same
 * server, so the bytes on the wire are identical; this validates the protocol +
 * the server's mediation without needing FujiNet hardware/emulation.)
 *
 * It plays one seat to completion: JOIN, then on each STATE snapshot, if it's our
 * turn, ROLL (server is authoritative for the dice) or pick a legal MOVE.  As a
 * determinism cross-check it asserts that whenever the server says PHASE_MOVE the
 * client's own rules also find a legal move.  Exit 0 on a clean game-over.
 *
 * Usage: ur-netclient <host> <port> <name>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "ur.h"
#include "proto.h"

static int read_full(int fd, uint8_t *buf, int n)
{
    int got = 0;
    while (got < n) {
        int r = (int)read(fd, buf + got, (size_t)(n - got));
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int         port = argc > 2 ? atoi(argv[2]) : 1234;
    const char *name = argc > 3 ? argv[3] : "TESTER";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 2; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) { fprintf(stderr, "bad host %s\n", host); return 2; }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { perror("connect"); return 2; }

    uint8_t buf[32];
    uint8_t n = ur_proto_join(buf, name);
    if (write(fd, buf, n) != n) { perror("write JOIN"); return 2; }

    unsigned rolls = 0, moves = 0;
    for (;;) {
        uint8_t raw[UR_STATE_MSG_LEN];
        if (read_full(fd, raw, UR_STATE_MSG_LEN) != 0) {
            fprintf(stderr, "[%s] disconnected\n", name);
            return 2;
        }
        ur_snapshot s;
        if (!ur_proto_decode_state(raw, UR_STATE_MSG_LEN, &s)) {
            fprintf(stderr, "[%s] bad STATE\n", name);
            return 2;
        }

        if (s.phase == UR_PHASE_OVER) {
            uint8_t sc0 = ur_score(&s.state, 0), sc1 = ur_score(&s.state, 1);
            printf("[%s] seat=%d GAME OVER winner=%d score=%u-%u (rolls=%u moves=%u)\n",
                   name, s.seat, s.winner, sc0, sc1, rolls, moves);
            if (s.winner != 0 && s.winner != 1) {
                fprintf(stderr, "[%s] FAIL: invalid winner %d\n", name, s.winner);
                return 3;
            }
            if (ur_score(&s.state, (uint8_t)s.winner) != UR_PIECES) {
                fprintf(stderr, "[%s] FAIL: winner has %u/%d home\n",
                        name, ur_score(&s.state, (uint8_t)s.winner), UR_PIECES);
                return 3;
            }
            return 0;
        }

        if (s.state.turn != s.seat) continue;     /* opponent's turn — keep reading */

        if (s.phase == UR_PHASE_ROLL) {
            n = ur_proto_roll(buf);
            if (write(fd, buf, n) != n) { perror("write ROLL"); return 2; }
            rolls++;
        } else if (s.phase == UR_PHASE_MOVE) {
            uint8_t pieces[UR_PIECES];
            uint8_t cnt = ur_legal_moves(&s.state, s.seat, s.roll, pieces);
            if (cnt == 0) {
                fprintf(stderr, "[%s] FAIL: server says MOVE (roll=%u) but client finds "
                                "NO legal move — rules disagree\n", name, s.roll);
                return 4;
            }
            n = ur_proto_move(buf, pieces[0]);
            if (write(fd, buf, n) != n) { perror("write MOVE"); return 2; }
            moves++;
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ur game server: a server-authoritative TCP host for one 2-player match.
// Listens for two clients (reachable from the Atari/Adam via FujiNet's
// N:TCP://host:port/), assigns seats, mediates the game per docs/protocol.md,
// then loops to host the next pair. v1: one game at a time, minimal error
// handling, no FGS-lobby registration yet.
package main

import (
	"bufio"
	"log"
	"net"
	"os"
)

func main() {
	addr := os.Getenv("UR_ADDR")
	if addr == "" {
		addr = ":1234"
	}
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("listen %s: %v", addr, err)
	}
	log.Printf("Ur server listening on %s", addr)
	startLobby()

	for {
		var c [2]net.Conn
		for i := 0; i < 2; i++ {
			conn, err := ln.Accept()
			if err != nil {
				log.Printf("accept: %v", err)
				i--
				continue
			}
			log.Printf("player %d connected from %s", i, conn.RemoteAddr())
			c[i] = conn
		}
		runGame(c)
	}
}

// readByte returns the next byte from r, or ok=false on error.
func readByte(r *bufio.Reader) (uint8, bool) {
	b, err := r.ReadByte()
	if err != nil {
		return 0, false
	}
	return b, true
}

func broadcast(c [2]net.Conn, st *State, phase, roll, flags uint8) {
	w := st.winner()
	for seat := 0; seat < 2; seat++ {
		_, _ = c[seat].Write(encodeState(uint8(seat), phase, roll, w, flags, st))
	}
}

func runGame(c [2]net.Conn) {
	defer c[0].Close()
	defer c[1].Close()
	setPlayers(2)        // advertise the game as full while it runs
	defer setPlayers(0)  // ...and free again when it ends

	r := [2]*bufio.Reader{bufio.NewReader(c[0]), bufio.NewReader(c[1])}

	// Expect a JOIN from each client (type byte, then version). Tolerant.
	for i := 0; i < 2; i++ {
		if t, ok := readByte(r[i]); ok && t == MsgJoin {
			readByte(r[i]) // version
		}
	}

	st := &State{}            // all pieces at start, Turn = 0
	phase := uint8(PhaseRoll) // current player must roll
	roll := uint8(0xFF)
	var flags uint8
	broadcast(c, st, phase, roll, flags)
	log.Printf("game start")

	for {
		cur := st.Turn
		t, ok := readByte(r[cur])
		if !ok {
			log.Printf("player %d disconnected", cur)
			return
		}

		switch t {
		case MsgRoll:
			if phase != PhaseRoll {
				continue
			}
			roll = dice()
			flags = 0
			if len(st.legalMoves(cur, roll)) == 0 {
				st.advanceTurn(MoveResult{}) // pass
				phase = PhaseRoll
				roll = 0xFF
			} else {
				phase = PhaseMove
			}
			broadcast(c, st, phase, roll, flags)

		case MsgMove:
			pi, ok := readByte(r[cur])
			if !ok {
				return
			}
			if phase != PhaseMove {
				continue
			}
			res, applied := st.applyMove(cur, pi, roll)
			if !applied {
				continue // illegal move: ignore (client should not send one)
			}
			flags = flagsFrom(res)
			if res.Won {
				broadcast(c, st, PhaseOver, 0xFF, flags)
				log.Printf("player %d wins", cur)
				return
			}
			st.advanceTurn(res)
			phase = PhaseRoll
			roll = 0xFF
			broadcast(c, st, phase, roll, flags)

		default:
			// ignore unknown message types
		}
	}
}

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

// store is the persistent leaderboard, shared by the game loop and HTTP handlers.
var store *Store

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
	store = loadStore(envOr("UR_DATA", "ur-stats.json"))
	startHTTP(store)
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

// readByte returns the next byte from r (and any error, for logging).
func readByte(r *bufio.Reader) (uint8, error) {
	return r.ReadByte()
}

func broadcast(c [2]net.Conn, st *State, phase, roll, flags uint8) {
	w := st.winner()
	log.Printf("broadcast STATE: turn=%d phase=%d roll=%d winner=%d flags=0x%02x", st.Turn, phase, roll, w, flags)
	for seat := 0; seat < 2; seat++ {
		if _, err := c[seat].Write(encodeState(uint8(seat), phase, roll, w, flags, st)); err != nil {
			log.Printf("  write to player %d failed: %v", seat, err)
		}
	}
}

func runGame(c [2]net.Conn) {
	defer c[0].Close()
	defer c[1].Close()
	setPlayers(2)        // advertise the game as full while it runs
	defer setPlayers(0)  // ...and free again when it ends

	r := [2]*bufio.Reader{bufio.NewReader(c[0]), bufio.NewReader(c[1])}

	// Expect a JOIN from each client (type, version, then NameLen name bytes).
	var names [2]string
	for i := 0; i < 2; i++ {
		t, err := readByte(r[i])
		if err != nil {
			log.Printf("player %d: failed to read JOIN: %v", i, err)
			return
		}
		if t == MsgJoin {
			v, _ := readByte(r[i]) // version
			nm := make([]byte, NameLen)
			for k := 0; k < NameLen; k++ {
				b, e := readByte(r[i])
				if e != nil {
					log.Printf("player %d: short JOIN: %v", i, e)
					return
				}
				nm[k] = b
			}
			names[i] = cleanName(nm)
			log.Printf("player %d JOIN v%d name %q", i, v, names[i])
		} else {
			log.Printf("player %d: unexpected first byte 0x%02x (expected JOIN)", i, t)
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
		t, err := readByte(r[cur])
		if err != nil {
			log.Printf("player %d disconnected: %v", cur, err)
			return
		}
		log.Printf("recv from player %d: cmd 0x%02x (phase=%d roll=%d)", cur, t, phase, roll)

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
			pi, err := readByte(r[cur])
			if err != nil {
				log.Printf("player %d disconnected (reading move): %v", cur, err)
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
				log.Printf("player %d (%q) wins", cur, names[cur])
				store.recordResult(names[cur], names[1-cur])
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

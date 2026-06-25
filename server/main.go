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
	"strconv"
	"time"
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

	// If a second human doesn't join within UR_AI_WAIT seconds (default 6), seat
	// the computer so a lone player still gets a game. UR_AI_WAIT=off disables it
	// (always wait for two humans).
	aiEnabled := true
	aiWait := 6 * time.Second
	if v := os.Getenv("UR_AI_WAIT"); v != "" {
		if v == "off" {
			aiEnabled = false
		} else if s, e := strconv.Atoi(v); e == nil && s >= 0 {
			aiWait = time.Duration(s) * time.Second
		}
	}
	tln := ln.(*net.TCPListener)

	for {
		tln.SetDeadline(time.Time{}) // block indefinitely for player 0
		conn0, err := tln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}
		log.Printf("player 0 connected from %s", conn0.RemoteAddr())

		if aiEnabled {
			tln.SetDeadline(time.Now().Add(aiWait)) // wait a bit for player 1
		}
		conn1, err := tln.Accept()
		tln.SetDeadline(time.Time{})
		if err != nil {
			if ne, ok := err.(net.Error); ok && ne.Timeout() {
				log.Printf("no second player after %s; starting a game vs the computer", aiWait)
				runGameAI(conn0)
			} else {
				log.Printf("accept(player 1): %v", err)
				conn0.Close()
			}
			continue
		}
		log.Printf("player 1 connected from %s", conn1.RemoteAddr())
		runGame([2]net.Conn{conn0, conn1})
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

// runGameAI hosts one human (seat 0) against the server's AI (seat 1). The human
// plays normally; the server drives the AI's rolls and moves with a short delay
// so they're visible. AI games are not recorded to the leaderboard.
func runGameAI(conn net.Conn) {
	defer conn.Close()
	setPlayers(2)
	defer setPlayers(0)

	r := bufio.NewReader(conn)

	// Expect a JOIN (type, version, then NameLen name bytes).
	name0 := ""
	if t, err := readByte(r); err != nil {
		log.Printf("AI game: failed to read JOIN: %v", err)
		return
	} else if t == MsgJoin {
		readByte(r) // version
		nm := make([]byte, NameLen)
		for k := 0; k < NameLen; k++ {
			b, e := readByte(r)
			if e != nil {
				log.Printf("AI game: short JOIN: %v", e)
				return
			}
			nm[k] = b
		}
		name0 = cleanName(nm)
	}
	log.Printf("AI game: %q (seat 0) vs computer (seat 1)", name0)

	st := &State{}
	phase := uint8(PhaseRoll)
	roll := uint8(0xFF)
	var flags uint8

	send := func(ph, rl, fl uint8) {
		if _, err := conn.Write(encodeState(0, ph, rl, st.winner(), fl, st)); err != nil {
			log.Printf("AI game: write failed: %v", err)
		}
	}
	send(phase, roll, flags)

	for {
		if st.Turn == 0 { // human's turn
			t, err := readByte(r)
			if err != nil {
				log.Printf("AI game: player disconnected: %v", err)
				return
			}
			switch t {
			case MsgRoll:
				if phase != PhaseRoll {
					continue
				}
				roll = dice()
				flags = 0
				if len(st.legalMoves(0, roll)) == 0 {
					st.advanceTurn(MoveResult{})
					phase, roll = PhaseRoll, 0xFF
				} else {
					phase = PhaseMove
				}
				send(phase, roll, flags)
			case MsgMove:
				pi, err := readByte(r)
				if err != nil {
					log.Printf("AI game: disconnected reading move: %v", err)
					return
				}
				if phase != PhaseMove {
					continue
				}
				res, ok := st.applyMove(0, pi, roll)
				if !ok {
					continue
				}
				flags = flagsFrom(res)
				if res.Won {
					send(PhaseOver, 0xFF, flags)
					log.Printf("AI game: %q wins", name0)
					return
				}
				st.advanceTurn(res)
				phase, roll = PhaseRoll, 0xFF
				send(phase, roll, flags)
			}
		} else { // AI's turn (seat 1)
			time.Sleep(700 * time.Millisecond)
			roll = dice()
			flags = 0
			if len(st.legalMoves(1, roll)) == 0 {
				st.advanceTurn(MoveResult{})
				send(PhaseRoll, 0xFF, flags)
				continue
			}
			send(PhaseMove, roll, flags) // let the human see the AI's roll
			time.Sleep(700 * time.Millisecond)
			res, _ := st.applyMove(1, aiPick(st, 1, roll), roll)
			flags = flagsFrom(res)
			if res.Won {
				send(PhaseOver, 0xFF, flags)
				log.Printf("AI game: computer beats %q", name0)
				return
			}
			st.advanceTurn(res)
			send(PhaseRoll, 0xFF, flags)
		}
	}
}

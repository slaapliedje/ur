// SPDX-License-Identifier: GPL-3.0-or-later
package main

import "testing"

func TestCapture(t *testing.T) {
	s := &State{}
	s.Piece[0][0] = 5
	s.Piece[1][0] = 7
	if !s.moveLegal(0, 0, 2) {
		t.Fatal("capture move should be legal")
	}
	res, ok := s.applyMove(0, 0, 2)
	if !ok || !res.Captured {
		t.Fatal("expected a capture")
	}
	if s.Piece[0][0] != 7 || s.Piece[1][0] != PosStart {
		t.Fatal("capture left wrong state")
	}
}

func TestRosetteSafe(t *testing.T) {
	s := &State{}
	s.Piece[0][0] = 6
	s.Piece[1][0] = 8 // opponent on the central rosette
	if s.moveLegal(0, 0, 2) {
		t.Fatal("landing on a safe rosette must be illegal")
	}
}

func TestRosetteExtra(t *testing.T) {
	s := &State{}
	s.Piece[0][0] = 2
	res, ok := s.applyMove(0, 0, 2) // -> 4, a rosette
	if !ok || !res.Rosette {
		t.Fatal("expected an extra-roll rosette")
	}
}

func TestBearOff(t *testing.T) {
	s := &State{}
	s.Piece[0][0] = 14
	if !s.moveLegal(0, 0, 1) {
		t.Fatal("exact bear-off should be legal")
	}
	if s.moveLegal(0, 0, 2) {
		t.Fatal("overshoot must be illegal")
	}
	res, _ := s.applyMove(0, 0, 1)
	if !res.Scored || s.Piece[0][0] != PosHome {
		t.Fatal("bear-off failed")
	}
}

func TestWin(t *testing.T) {
	s := &State{}
	for i := 0; i < Pieces-1; i++ {
		s.Piece[0][i] = PosHome
	}
	s.Piece[0][Pieces-1] = 14
	res, _ := s.applyMove(0, Pieces-1, 1)
	if !res.Won || s.winner() != 0 {
		t.Fatal("expected a win")
	}
}

func TestDiceRange(t *testing.T) {
	var counts [5]int
	for i := 0; i < 20000; i++ {
		r := dice()
		if r > 4 {
			t.Fatalf("roll out of range: %d", r)
		}
		counts[r]++
	}
	for v := 0; v <= 4; v++ {
		if counts[v] == 0 {
			t.Fatalf("roll %d never occurred", v)
		}
	}
	if counts[2] <= counts[0] {
		t.Fatal("2 should be the most common roll")
	}
}

func TestSelfPlay(t *testing.T) {
	s := &State{}
	plies := 0
	for ; plies < 5000 && s.winner() < 0; plies++ {
		roll := dice()
		moves := s.legalMoves(s.Turn, roll)
		if len(moves) == 0 {
			s.advanceTurn(MoveResult{})
			continue
		}
		res, _ := s.applyMove(s.Turn, moves[0], roll)
		if !res.Won {
			s.advanceTurn(res)
		}
	}
	if s.winner() < 0 {
		t.Fatal("self-play did not reach a winner")
	}
}

func TestEncodeState(t *testing.T) {
	s := &State{Turn: 1}
	s.Piece[0][0] = 5
	s.Piece[1][6] = 14
	b := encodeState(1, PhaseMove, 3, -1, FlagCaptured, s)
	if len(b) != StateMsgLen {
		t.Fatalf("length = %d, want %d", len(b), StateMsgLen)
	}
	if b[0] != MsgState || b[1] != 1 || b[2] != 1 || b[3] != PhaseMove ||
		b[4] != 3 || b[5] != 0xFF || b[6] != FlagCaptured {
		t.Fatal("header bytes wrong")
	}
	if b[7] != 5 || b[20] != 14 {
		t.Fatal("piece bytes wrong")
	}
}

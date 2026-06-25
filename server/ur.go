// SPDX-License-Identifier: GPL-3.0-or-later
//
// Authoritative Royal Game of Ur rules for the server. Mirrors the portable C
// core (src/common/ur.c): 14-step path, rosettes at 4/8/14, shared middle 5-12,
// capture on the shared row, rosette = extra roll + safe, exact-roll bear-off.
package main

import "math/rand"

const (
	Pieces      = 7
	PathLen     = 14
	PosStart    = 0
	PosHome     = 15 // PathLen + 1
	SharedFirst = 5
	SharedLast  = 12
)

// State is the full game position.
type State struct {
	Piece [2][Pieces]uint8 // 0 = start, 1..14 = path, 15 = home
	Turn  uint8            // 0 or 1
}

// MoveResult reports what a move did.
type MoveResult struct {
	Captured, Scored, Rosette, Won bool
}

func isRosette(p uint8) bool { return p == 4 || p == 8 || p == 14 }
func isShared(p uint8) bool  { return p >= SharedFirst && p <= SharedLast }

func (s *State) ownOccupies(pl, pos uint8) bool {
	for i := 0; i < Pieces; i++ {
		if s.Piece[pl][i] == pos {
			return true
		}
	}
	return false
}

// oppAt returns the index of opp's piece on path position pos, or -1.
func (s *State) oppAt(opp, pos uint8) int {
	for i := 0; i < Pieces; i++ {
		if s.Piece[opp][i] == pos {
			return i
		}
	}
	return -1
}

func (s *State) moveLegal(pl, piece, roll uint8) bool {
	if roll == 0 || piece >= Pieces {
		return false
	}
	p := s.Piece[pl][piece]
	if p == PosHome {
		return false
	}
	dest := p + roll
	if dest > PosHome {
		return false
	}
	if dest == PosHome {
		return true
	}
	if s.ownOccupies(pl, dest) {
		return false
	}
	if isShared(dest) {
		opp := 1 - pl
		if s.oppAt(opp, dest) >= 0 {
			return !isRosette(dest) // capture, unless a safe rosette
		}
	}
	return true
}

func (s *State) legalMoves(pl, roll uint8) []uint8 {
	var out []uint8
	for i := uint8(0); i < Pieces; i++ {
		if s.moveLegal(pl, i, roll) {
			out = append(out, i)
		}
	}
	return out
}

// applyMove applies a legal move and returns its result; ok is false if illegal.
func (s *State) applyMove(pl, piece, roll uint8) (res MoveResult, ok bool) {
	if !s.moveLegal(pl, piece, roll) {
		return res, false
	}
	p := s.Piece[pl][piece]
	dest := p + roll
	if dest != PosHome && isShared(dest) {
		opp := 1 - pl
		if v := s.oppAt(opp, dest); v >= 0 {
			s.Piece[opp][v] = PosStart
			res.Captured = true
		}
	}
	s.Piece[pl][piece] = dest
	if dest == PosHome {
		res.Scored = true
	} else if isRosette(dest) {
		res.Rosette = true
	}
	if s.score(pl) == Pieces {
		res.Won = true
	}
	return res, true
}

func (s *State) advanceTurn(r MoveResult) {
	if r.Rosette || r.Won {
		return
	}
	s.Turn = 1 - s.Turn
}

// aiPick chooses a move for `pl` (must have at least one legal move). Greedy:
// bear off > capture > land on a rosette > advance the furthest piece, with a
// nudge to bring new pieces on.
func aiPick(s *State, pl, roll uint8) uint8 {
	moves := s.legalMoves(pl, roll)
	best := moves[0]
	bestScore := -1 << 30
	for _, pi := range moves {
		p := s.Piece[pl][pi]
		dest := p + roll
		score := 0
		switch {
		case dest == PosHome:
			score = 1000 // bear a piece off
		default:
			if isShared(dest) && !isRosette(dest) && s.oppAt(1-pl, dest) >= 0 {
				score += 200 // capture an opponent
			}
			if isRosette(dest) {
				score += 100 // extra roll + safe square
			}
			score += int(dest) // otherwise advance the furthest piece
			if p == PosStart {
				score += 5 // mild preference to get pieces moving
			}
		}
		if score > bestScore {
			bestScore = score
			best = pi
		}
	}
	return best
}

func (s *State) score(pl uint8) uint8 {
	var n uint8
	for i := 0; i < Pieces; i++ {
		if s.Piece[pl][i] == PosHome {
			n++
		}
	}
	return n
}

// winner returns 0/1, or -1 if the game is not over.
func (s *State) winner() int {
	if s.score(0) == Pieces {
		return 0
	}
	if s.score(1) == Pieces {
		return 1
	}
	return -1
}

// dice rolls four binary tetrahedral dice -> 0..4 (distribution 1/4/6/4/1).
func dice() uint8 {
	var n uint8
	for i := 0; i < 4; i++ {
		n += uint8(rand.Intn(2))
	}
	return n
}

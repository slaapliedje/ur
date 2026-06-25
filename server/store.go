// SPDX-License-Identifier: GPL-3.0-or-later
//
// Persistent player leaderboard, keyed by 3-letter name. Backed by a JSON file
// with atomic (write-temp-then-rename) saves; safe for the server's concurrency
// (one game at a time today, but the HTTP handlers read it too).
package main

import (
	"encoding/json"
	"log"
	"os"
	"sort"
	"strings"
	"sync"
	"time"
)

// PlayerStats is one player's record. Name is the canonical (A-Z, <=3) key.
type PlayerStats struct {
	Name   string `json:"name"`
	Wins   int    `json:"wins"`
	Losses int    `json:"losses"`
	Games  int    `json:"games"`
	Last   string `json:"last"` // RFC3339 UTC of last game
}

// Store is the in-memory leaderboard plus its on-disk path.
type Store struct {
	mu      sync.Mutex
	path    string
	Players map[string]*PlayerStats
}

// cleanName canonicalises a raw 3-byte name to uppercase A-Z, trimmed.
func cleanName(b []byte) string {
	out := make([]byte, 0, len(b))
	for _, c := range b {
		if c >= 'a' && c <= 'z' {
			c -= 32 // uppercase
		}
		if (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' {
			out = append(out, c)
		} else {
			out = append(out, ' ')
		}
	}
	return strings.TrimSpace(string(out))
}

// loadStore reads the JSON file at path (empty store if it is missing/invalid).
func loadStore(path string) *Store {
	s := &Store{path: path, Players: map[string]*PlayerStats{}}
	if data, err := os.ReadFile(path); err == nil {
		var m map[string]*PlayerStats
		if err := json.Unmarshal(data, &m); err == nil && m != nil {
			s.Players = m
		} else if err != nil {
			log.Printf("store: ignoring unreadable %s: %v", path, err)
		}
	}
	log.Printf("store: %d players loaded from %s", len(s.Players), path)
	return s
}

func (s *Store) get(name string) *PlayerStats {
	p := s.Players[name]
	if p == nil {
		p = &PlayerStats{Name: name}
		s.Players[name] = p
	}
	return p
}

// recordResult bumps winner/loser tallies and persists. Empty names are skipped
// (anonymous players don't appear on the board).
func (s *Store) recordResult(winner, loser string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now().UTC().Format(time.RFC3339)
	if winner != "" {
		p := s.get(winner)
		p.Wins++
		p.Games++
		p.Last = now
	}
	if loser != "" {
		p := s.get(loser)
		p.Losses++
		p.Games++
		p.Last = now
	}
	s.saveLocked()
}

// top returns up to n players sorted by wins (then fewest losses, then name).
func (s *Store) top(n int) []PlayerStats {
	s.mu.Lock()
	defer s.mu.Unlock()
	list := make([]PlayerStats, 0, len(s.Players))
	for _, p := range s.Players {
		list = append(list, *p)
	}
	sort.Slice(list, func(i, j int) bool {
		if list[i].Wins != list[j].Wins {
			return list[i].Wins > list[j].Wins
		}
		if list[i].Losses != list[j].Losses {
			return list[i].Losses < list[j].Losses
		}
		return list[i].Name < list[j].Name
	})
	if n >= 0 && len(list) > n {
		list = list[:n]
	}
	return list
}

func (s *Store) saveLocked() {
	if s.path == "" {
		return
	}
	data, err := json.MarshalIndent(s.Players, "", "  ")
	if err != nil {
		log.Printf("store: marshal: %v", err)
		return
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		log.Printf("store: write %s: %v", tmp, err)
		return
	}
	if err := os.Rename(tmp, s.path); err != nil {
		log.Printf("store: rename %s: %v", s.path, err)
	}
}

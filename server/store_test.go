// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"path/filepath"
	"testing"
)

func TestCleanName(t *testing.T) {
	tests := []struct{ in, want string }{
		{"ABC", "ABC"},
		{"abc", "ABC"},
		{"Z  ", "Z"},
		{"   ", ""},
		{"AB3Z", "AB3Z"}, // digits kept
		{"A!B", "A B"},   // punctuation -> space (kept internally)
	}
	for _, tc := range tests {
		if got := cleanName([]byte(tc.in)); got != tc.want {
			t.Errorf("cleanName(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestStoreRecordTopAndPersist(t *testing.T) {
	path := filepath.Join(t.TempDir(), "stats.json")
	s := loadStore(path)
	s.recordResult("AAA", "BBB")
	s.recordResult("AAA", "CCC")
	s.recordResult("BBB", "AAA")
	s.recordResult("", "DDD") // anonymous winner skipped; DDD gets a loss

	top := s.top(10)
	if len(top) != 4 {
		t.Fatalf("want 4 players, got %d", len(top))
	}
	if top[0].Name != "AAA" || top[0].Wins != 2 || top[0].Losses != 1 || top[0].Games != 3 {
		t.Errorf("top[0] = %+v", top[0])
	}
	if top[1].Name != "BBB" || top[1].Wins != 1 {
		t.Errorf("top[1] = %+v", top[1])
	}
	// CCC and DDD tie (0 wins, 1 loss): ordered by name.
	if top[2].Name != "CCC" || top[3].Name != "DDD" {
		t.Errorf("tie order = %q,%q want CCC,DDD", top[2].Name, top[3].Name)
	}

	// Reloading from disk preserves the tallies.
	s2 := loadStore(path)
	if p := s2.Players["AAA"]; p == nil || p.Wins != 2 {
		t.Errorf("reloaded AAA = %+v, want 2 wins", p)
	}
}

// SPDX-License-Identifier: GPL-3.0-or-later
package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestLobbyPayload(t *testing.T) {
	t.Setenv("UR_SERVER_NAME", "test-srv")
	t.Setenv("UR_APPKEY", "7")

	gs := lobbyPayload(2, true)
	if gs.Game != "Royal Game of Ur" {
		t.Fatalf("game = %q", gs.Game)
	}
	if gs.Maxplayers != 2 || gs.Curplayers != 2 {
		t.Fatalf("players = %d/%d", gs.Curplayers, gs.Maxplayers)
	}
	if gs.Status != "online" || gs.Server != "test-srv" || gs.Appkey != 7 {
		t.Fatalf("fields: status=%q server=%q appkey=%d", gs.Status, gs.Server, gs.Appkey)
	}

	b, err := json.Marshal(gs)
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	for _, key := range []string{
		`"game"`, `"appkey"`, `"server"`, `"region"`, `"serverurl"`,
		`"status"`, `"maxplayers"`, `"curplayers"`, `"clients"`,
	} {
		if !strings.Contains(s, key) {
			t.Fatalf("payload missing key %s: %s", key, s)
		}
	}
}

func TestLobbyOffline(t *testing.T) {
	if lobbyPayload(0, false).Status != "offline" {
		t.Fatal("expected offline status")
	}
}

// Each platform with a UR_CLIENT_<PLAT> URL set is advertised; unset ones are
// skipped (an empty url would make the lobby reject the whole POST).
func TestLobbyClients(t *testing.T) {
	t.Setenv("UR_CLIENT_ATARI", "tnfs://h/ur.xex")
	t.Setenv("UR_CLIENT_C64", "tnfs://h/ur.prg")
	t.Setenv("UR_CLIENT_APPLE2", "tnfs://h/ur.system")
	// UR_CLIENT_ADAM deliberately unset -> not advertised.

	got := map[string]string{}
	for _, c := range lobbyPayload(0, true).Clients {
		if c.Url == "" {
			t.Fatalf("advertised %s with empty url", c.Platform)
		}
		got[c.Platform] = c.Url
	}
	for _, p := range []string{"atari", "c64", "apple2"} {
		if got[p] == "" {
			t.Fatalf("missing client for %s: %v", p, got)
		}
	}
	if _, ok := got["adam"]; ok {
		t.Fatalf("adam should not be advertised (no url set): %v", got)
	}
}

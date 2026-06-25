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

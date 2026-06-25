// SPDX-License-Identifier: GPL-3.0-or-later
//
// FGS Lobby registration: advertise this server to the FujiNet lobby so clients
// can discover it. Mirrors the canonical schema (FujiNetWIFI servers'
// lobbyClient.go): POST a GameServer JSON to the lobby's /server endpoint, and
// refresh periodically + on player-count changes.
//
// OPT-IN: disabled unless UR_LOBBY=1, so local runs never touch the public lobby.
package main

import (
	"bytes"
	"encoding/json"
	"log"
	"net/http"
	"os"
	"strconv"
	"sync/atomic"
	"time"
)

const defaultLobbyURL = "https://lobby.fujinet.online/server"

// GameClient points the lobby client at the per-platform game-client binary.
type GameClient struct {
	Platform string `json:"platform"`
	Url      string `json:"url"`
}

// GameServer is the lobby registration payload (field names per FGS).
type GameServer struct {
	Game       string       `json:"game"`
	Appkey     int          `json:"appkey"`
	Server     string       `json:"server"`
	Region     string       `json:"region"`
	Serverurl  string       `json:"serverurl"`
	Status     string       `json:"status"`
	Maxplayers int          `json:"maxplayers"`
	Curplayers int          `json:"curplayers"`
	Clients    []GameClient `json:"clients"`
}

var (
	lobbyEnabled bool
	lobbyURL     string
	curPlayers   int32
)

func envOr(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

// lobbyPayload builds the registration body for the current player count.
func lobbyPayload(cur int, online bool) GameServer {
	appkey, _ := strconv.Atoi(envOr("UR_APPKEY", "0"))
	status := "offline"
	if online {
		status = "online"
	}
	// Only advertise a client whose download URL is set — the lobby validates
	// clients[].url as a real URL, so an empty entry would reject the whole POST.
	clients := []GameClient{}
	if u := os.Getenv("UR_CLIENT_ATARI"); u != "" {
		clients = append(clients, GameClient{Platform: "atari", Url: u})
	}
	return GameServer{
		Game:       "Royal Game of Ur",
		Appkey:     appkey, // project-assigned id (UR_APPKEY); 0 is invalid
		Server:     envOr("UR_SERVER_NAME", "ur-1"),
		Region:     envOr("UR_REGION", "us"),
		Serverurl:  envOr("UR_SERVER_URL", "tcp://localhost:1234/"),
		Status:     status,
		Maxplayers: 2,
		Curplayers: cur,
		Clients:    clients,
	}
}

func postLobby(gs GameServer) {
	body, err := json.Marshal(gs)
	if err != nil {
		log.Printf("lobby marshal: %v", err)
		return
	}
	req, err := http.NewRequest("POST", lobbyURL, bytes.NewBuffer(body))
	if err != nil {
		log.Printf("lobby request: %v", err)
		return
	}
	req.Header.Set("Content-Type", "application/json; charset=UTF-8")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		log.Printf("lobby post: %v", err)
		return
	}
	defer resp.Body.Close()
	log.Printf("lobby update (%d players): %s", gs.Curplayers, resp.Status)
}

func updateLobby() {
	if !lobbyEnabled {
		return
	}
	postLobby(lobbyPayload(int(atomic.LoadInt32(&curPlayers)), true))
}

// setPlayers records the current player count and refreshes the lobby.
func setPlayers(n int) {
	atomic.StoreInt32(&curPlayers, int32(n))
	updateLobby()
}

// startLobby enables registration (if UR_LOBBY=1) and starts a heartbeat.
func startLobby() {
	lobbyEnabled = os.Getenv("UR_LOBBY") == "1"
	lobbyURL = envOr("UR_LOBBY_URL", defaultLobbyURL)
	if !lobbyEnabled {
		log.Printf("lobby registration disabled (set UR_LOBBY=1 to enable)")
		return
	}
	if ak, _ := strconv.Atoi(envOr("UR_APPKEY", "0")); ak < 1 || ak > 255 {
		log.Printf("WARNING: UR_APPKEY=%d invalid (need 1-255, assigned by the FujiNet project); the lobby will reject registration", ak)
	}
	log.Printf("registering with lobby at %s", lobbyURL)
	updateLobby() // initial: online, 0 players
	go func() {
		for range time.NewTicker(30 * time.Second).C {
			updateLobby()
		}
	}()
}

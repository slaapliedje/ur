// SPDX-License-Identifier: GPL-3.0-or-later
//
// HTTP leaderboard for the Ur server. One extra listener (no separate web
// server needed): a human-friendly HTML page, a JSON API, and a compact binary
// /top that the 8-bit clients fetch over FujiNet N:HTTP. Disable with
// UR_HTTP_ADDR=off; default :8080.
package main

import (
	"encoding/json"
	"fmt"
	"html"
	"log"
	"net/http"
	"strconv"
)

// topN is how many rows the compact /top endpoint returns to 8-bit clients.
const topN = 10

func startHTTP(store *Store) {
	addr := envOr("UR_HTTP_ADDR", ":8080")
	if addr == "off" {
		log.Printf("http leaderboard disabled (UR_HTTP_ADDR=off)")
		return
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		htmlLeaderboard(w, store)
	})
	mux.HandleFunc("/leaderboard.json", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_ = json.NewEncoder(w).Encode(store.top(100))
	})
	mux.HandleFunc("/top", func(w http.ResponseWriter, r *http.Request) {
		compactTop(w, store)
	})
	go func() {
		log.Printf("leaderboard http listening on %s", addr)
		if err := http.ListenAndServe(addr, mux); err != nil {
			log.Printf("http server: %v", err)
		}
	}()
}

// compactTop writes a tiny fixed-format body for 6502/Z80 parsing:
//
//	byte 0          : row count N (0..topN)
//	then N records  : name[3] (space-padded) + wins (uint16, little-endian)
func compactTop(w http.ResponseWriter, store *Store) {
	list := store.top(topN)
	buf := make([]byte, 0, 1+len(list)*5)
	buf = append(buf, byte(len(list)))
	for _, p := range list {
		nm := fmt.Sprintf("%-3.3s", p.Name) // pad/truncate to exactly 3
		wins := p.Wins
		if wins > 0xFFFF {
			wins = 0xFFFF
		}
		buf = append(buf, nm[0], nm[1], nm[2], byte(wins&0xFF), byte(wins>>8))
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", strconv.Itoa(len(buf)))
	_, _ = w.Write(buf)
}

func htmlLeaderboard(w http.ResponseWriter, store *Store) {
	list := store.top(100)
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprint(w, `<!doctype html><html lang="en"><head><meta charset="utf-8">`+
		`<meta name="viewport" content="width=device-width,initial-scale=1">`+
		`<title>Royal Game of Ur - Leaderboard</title><style>`+
		`body{font-family:monospace;background:#0b1d3a;color:#e8d8a0;margin:2rem}`+
		`h1{color:#d8a23a}table{border-collapse:collapse;min-width:24rem}`+
		`th,td{padding:.35rem 1rem;text-align:left;border-bottom:1px solid #24406e}`+
		`th{color:#d8a23a}td.n{text-align:right}tr:nth-child(even){background:#10254a}`+
		`small{color:#7f93b8}</style></head><body>`)
	fmt.Fprint(w, `<h1>The Royal Game of Ur &mdash; Leaderboard</h1>`)
	if len(list) == 0 {
		fmt.Fprint(w, `<p>No games recorded yet.</p>`)
	} else {
		fmt.Fprint(w, `<table><tr><th>#</th><th>Name</th><th class=n>Wins</th>`+
			`<th class=n>Losses</th><th class=n>Games</th></tr>`)
		for i, p := range list {
			fmt.Fprintf(w, `<tr><td class=n>%d</td><td>%s</td><td class=n>%d</td>`+
				`<td class=n>%d</td><td class=n>%d</td></tr>`,
				i+1, html.EscapeString(p.Name), p.Wins, p.Losses, p.Games)
		}
		fmt.Fprint(w, `</table>`)
	}
	fmt.Fprint(w, `<p><small>JSON: <a href="/leaderboard.json">/leaderboard.json</a></small></p>`)
	fmt.Fprint(w, `</body></html>`)
}

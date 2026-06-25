// SPDX-License-Identifier: GPL-3.0-or-later
//
// Server side of the Ur wire protocol. Must stay byte-for-byte identical to
// src/common/proto.{h,c} and docs/protocol.md.
package main

const (
	MsgJoin  = 0x01 // client->server: join (byte1 = version)
	MsgRoll  = 0x02 // client->server: request a roll
	MsgMove  = 0x03 // client->server: move (byte1 = piece index)
	MsgState = 0x81 // server->client: 21-byte snapshot
)

const (
	PhaseRoll = 0
	PhaseMove = 1
	PhaseOver = 2
)

const (
	FlagCaptured = 0x01
	FlagRosette  = 0x02
	FlagScored   = 0x04
)

const (
	ProtoVersion = 1
	StateMsgLen  = 21
)

// encodeState builds the 21-byte STATE snapshot for a given seat.
func encodeState(seat, phase, roll uint8, winner int, flags uint8, st *State) []byte {
	b := make([]byte, StateMsgLen)
	b[0] = MsgState
	b[1] = seat
	b[2] = st.Turn
	b[3] = phase
	b[4] = roll
	if winner < 0 {
		b[5] = 0xFF
	} else {
		b[5] = uint8(winner)
	}
	b[6] = flags
	for i := 0; i < Pieces; i++ {
		b[7+i] = st.Piece[0][i]
		b[14+i] = st.Piece[1][i]
	}
	return b
}

func flagsFrom(r MoveResult) uint8 {
	var f uint8
	if r.Captured {
		f |= FlagCaptured
	}
	if r.Rosette {
		f |= FlagRosette
	}
	if r.Scored {
		f |= FlagScored
	}
	return f
}

; SPDX-License-Identifier: GPL-3.0-or-later
;
; Display-list-interrupt handler: per interrupted board-band row it writes TWO
; graded colours — the background field (COLBK) from dli_table[] and the tile
; faces (COLPF2) from dli_table2[] — painting a vertical lapis gradient down the
; board. Grading both registers (the "4½-colour" trick, borrowed from rogi's
; Ultima V DLI) makes the carved tiles themselves catch light, not just the field
; behind them. One steady colour per row (no per-scanline dithering — that
; flickered). The C side (atarihw.c) fills both tables, sets dli_len, points
; VDSLST here, flags the board-band rows, and enables the DLI in NMIEN. The
; flagged-row count equals dli_len, so the index wraps back to 0 once per frame.

        .export _dli_handler
        .import _dli_table
        .import _dli_table2
        .import _dli_len

WSYNC  = $D40A
.ifdef UR_A5200
COLBK  = $C01A          ; 5200 GTIA at $C000
COLPF2 = $C018
.else
COLBK  = $D01A          ; A8 GTIA at $D000
COLPF2 = $D018
.endif

        .segment "BSS"
idx:    .res 1

        .segment "CODE"
.proc _dli_handler
        pha
        txa
        pha
        tya
        pha
        ldx idx
        lda _dli_table,x   ; field shade (COLBK)
        ldy _dli_table2,x  ; tile-face shade (COLPF2)
        sta WSYNC          ; align to horizontal blank, then change both colours
        sta COLBK
        sty COLPF2
        inx
        cpx _dli_len
        bne save
        ldx #$00           ; wrap at the end of the gradient (= top of next frame)
save:   stx idx
        pla
        tay
        pla
        tax
        pla
        rti
.endproc

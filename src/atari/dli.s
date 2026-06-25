; SPDX-License-Identifier: GPL-3.0-or-later
;
; Display-list-interrupt handler for the title screen: writes the background
; colour (COLBK) from dli_table[] once per interrupted board-band row, painting a
; vertical lapis gradient behind the ziggurat. One steady colour per row (no
; per-scanline dithering — that flickered). The C side (atarihw.c) fills the
; table, sets dli_len, points VDSLST here, flags the board-band rows, and enables
; the DLI in NMIEN. The flagged-row count equals dli_len, so the index wraps back
; to 0 exactly once per frame (no VBI reset needed).

        .export _dli_handler
        .import _dli_table
        .import _dli_len

WSYNC = $D40A
COLBK = $D01A

        .segment "BSS"
idx:    .res 1

        .segment "CODE"
.proc _dli_handler
        pha
        txa
        pha
        ldx idx
        lda _dli_table,x
        sta WSYNC          ; align to horizontal blank, then change the colour
        sta COLBK
        inx
        cpx _dli_len
        bne save
        ldx #$00           ; wrap at the end of the gradient (= top of next frame)
save:   stx idx
        pla
        tax
        pla
        rti
.endproc

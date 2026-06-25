; SPDX-License-Identifier: GPL-3.0-or-later
;
; Display-list-interrupt handler for the title screen: writes the background
; colour (COLBK) from dli_table[] on each interrupted line, painting a vertical
; lapis gradient behind the ziggurat. The C side (atarihw.c) fills the table,
; sets dli_len, points VDSLST here, flags the board-band rows in the display
; list, and enables the DLI in NMIEN. The flagged-line count equals dli_len, so
; the index wraps back to 0 exactly once per frame (no VBI reset needed).

        .export _dli_handler
        .import _dli_table
        .import _dli_len

WSYNC = $D40A
COLBK = $D01A

SCANLINES = 8              ; mode-4 board rows are 8 scanlines tall

        .segment "BSS"
idx:    .res 1

        .segment "CODE"
; One DLI per board-band row, but we recolour every scanline of that row from
; dli_table[] for a smooth per-scanline gradient. dli_len is the table length
; (14 rows * 8 = 112), so idx wraps back to 0 exactly once per frame.
.proc _dli_handler
        pha
        txa
        pha
        tya
        pha
        ldx idx
        ldy #SCANLINES
loop:   lda _dli_table,x
        sta WSYNC          ; align to horizontal blank, then change the colour
        sta COLBK
        inx
        dey
        bne loop
        cpx _dli_len
        bcc save
        ldx #$00           ; wrap at the end of the gradient (= top of next frame)
save:   stx idx
        pla
        tay
        pla
        tax
        pla
        rti
.endproc

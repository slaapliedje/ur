; SPDX-License-Identifier: GPL-3.0-or-later
;
; Display-list-interrupt handler: per interrupted row it writes THREE colour
; registers from parallel tables — COLPF2 (tile face) from dli_table2[], COLPF1
; (tile "10" / tray pieces) from dli_table3[], and COLBK (field/border) from
; dli_table[]. Driving COLPF1+COLPF2 on the board band lets the HUD text rows
; keep their own COLPF1/COLPF2 (via the OS shadow registers + a final "reset" row
; in the table), so the per-turn text background works even though the board and
; the mode-2 text share those registers. The C side (atarihw.c) fills the tables,
; sets dli_len, points VDSLST here, flags the rows, and enables the DLI in NMIEN.
; The flagged-row count equals dli_len, so the index wraps to 0 once per frame.

        .export _dli_handler
        .import _dli_table
        .import _dli_table2
        .import _dli_table3
        .import _dli_len

WSYNC  = $D40A
.ifdef UR_A5200
COLBK  = $C01A          ; 5200 GTIA at $C000
COLPF2 = $C018
COLPF1 = $C017
.else
COLBK  = $D01A          ; A8 GTIA at $D000
COLPF2 = $D018
COLPF1 = $D017
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
        lda _dli_table,x   ; field / border (COLBK) — first (varies on the title sky)
        ldy _dli_table2,x  ; tile-face / text-bg (COLPF2)
        sta WSYNC          ; align to horizontal blank, then change the colours
        sta COLBK
        sty COLPF2
        lda _dli_table3,x  ; tile "10" / text-lum (COLPF1) — uniform, so a late store is fine
        sta COLPF1
        inx
        cpx _dli_len
        bne save
        ldx #$00           ; wrap at the end of the table (= top of next frame)
save:   stx idx
        pla
        tay
        pla
        tax
        pla
        rti
.endproc

; SPDX-License-Identifier: GPL-3.0-or-later
; ---------------------------------------------------------------------------
; DHGR auxiliary-memory blits.
;
; Writing DHGR page-2 aux ($4000-$5FFF aux bank) needs RAMWRT, which reroutes ALL
; of $0200-$BFFF writes to aux — including cc65's C stack.  So this is asm: while
; RAMWRT is on it touches ONLY registers, the hardware stack, and the intended aux
; target (via self-modified absolute,Y stores, so no zero-page pointer needed).
;
; dh_fill blits a whole RECTANGLE of aux in one call: RAMWRT is toggled once (not
; per scanline) and each scanline's base address comes from the precomputed row
; tables (_rlo/_rhi), so there's no per-scanline JSR or 16-bit multiply.  C sets
; the globals, then calls dh_fill().
; ---------------------------------------------------------------------------

.export _dh_fill, _dh_mirror
.export _dh_dst, _dh_b0, _dh_b1, _dh_n, _dh_y0, _dh_y1, _dh_col
.import _rlo, _rhi              ; 192-entry DHGR row base tables (lo/hi)
.importzp ptr1                  ; cc65 scratch ZP pointer (clobberable across calls)

RAMWRTON  = $C005       ; writes -> AUX
RAMWRTOFF = $C004       ; writes -> MAIN

.bss
_dh_dst:  .res 2        ; (dh_mirror) source/dest address
_dh_b0:   .res 1        ; byte for even column offsets
_dh_b1:   .res 1        ; byte for odd column offsets
_dh_n:    .res 1        ; byte count per scanline (<= 40)
_dh_y0:   .res 1        ; first scanline
_dh_y1:   .res 1        ; last scanline (inclusive)
_dh_col:  .res 1        ; starting aux column

.code

; void dh_fill(void) — fill aux rows _dh_y0.._dh_y1, columns _dh_col..+_dh_n-1,
; alternating _dh_b0/_dh_b1 per column. RAMWRT on once for the whole rectangle.
_dh_fill:
        sta RAMWRTON
        ldx _dh_y0
yloop:
        lda _rlo,x              ; ptr1 = row_base(x) + dh_col  (ZP write, unaffected
        clc                     ;   by RAMWRT — self-modifying code WOULD go to aux)
        adc _dh_col
        sta ptr1
        lda _rhi,x
        adc #$00
        sta ptr1+1
        ldy #$00
xloop:
        cpy _dh_n
        bcs xdone
        tya
        and #$01
        beq useb0
        lda _dh_b1
        jmp put
useb0:
        lda _dh_b0
put:
        sta (ptr1),y           ; write AUX at base+y
        iny
        bne xloop
xdone:
        cpx _dh_y1
        beq done
        inx
        jmp yloop
done:
        sta RAMWRTOFF
        rts

; void dh_mirror(void) — copy _dh_n bytes at _dh_dst from MAIN to AUX. Used to
; mirror a page-2 text row into aux so 40-col text shows double-wide in 80-col.
_dh_mirror:
        lda _dh_dst
        sta rd+1
        sta wr+1
        lda _dh_dst+1
        sta rd+2
        sta wr+2
        sta RAMWRTON
        ldy #$00
mloop:
        cpy _dh_n
        bcs mdone
rd:     lda $FFFF,y             ; read MAIN
wr:     sta $FFFF,y             ; write AUX
        iny
        bne mloop
mdone:
        sta RAMWRTOFF
        rts

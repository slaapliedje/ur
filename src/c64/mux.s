; SPDX-License-Identifier: GPL-3.0-or-later
; ---------------------------------------------------------------------------
; Sprite multiplexer for the Royal Game of Ur (Commodore 64 / VIC-II).
;
; The C64 has only 8 hardware sprites, but the board can hold up to 14 pieces.
; Ur's board is three horizontal rows, though, and a single row holds at most
; eight squares -- exactly the VIC-II per-rasterline limit.  So we lay the board
; out as three rows far enough apart that a 21-line sprite finishes well before
; the next row, and reuse the same 8 hardware sprites across all three rows with
; a chain of raster interrupts.  8 sprites x 3 bands = 24 token slots -- plenty.
;
; Band 0 is the frame-top interrupt: it programs row-0's tokens AND chains to the
; KERNAL IRQ ($EA31) so keyboard scan / jiffy clock keep running (cgetc still
; works).  Bands 1 and 2 just reprogram the sprites and restore+RTI ($EA81).
;
; C populates the band tables (positions/pointers/colours/enable) each redraw;
; this code just blits the active band's table into the VIC registers.
; ---------------------------------------------------------------------------

.export _mux_install, _mux_stop
.export _band_n, _band_y, _band_trig, _band_x, _band_ptr, _band_col, _band_en

VIC_SP0X  = $D000               ; sprite 0 X (X/Y interleaved through $D00F)
VIC_EN    = $D015               ; sprite enable bits
VIC_RASTER= $D012               ; raster compare (low 8 bits)
VIC_CTRL1 = $D011               ; (bit7 = raster bit 8; we keep rasters < 256)
VIC_IRQ   = $D019               ; IRQ latch (write to ack)
VIC_IRQEN = $D01A               ; IRQ enable
VIC_SP0C  = $D027               ; sprite 0 colour (through $D02E)
SPRPTR    = $07F8               ; sprite pointers (last 8 bytes of screen matrix)

KERN_IRQ  = $EA31               ; KERNAL IRQ body (keyboard scan + jiffy clock)
KERN_RTI  = $EA81               ; KERNAL restore A/X/Y + RTI
CIA1_ICR  = $DC0D               ; CIA1 interrupt control (timer IRQ source)

.bss
_band_n:    .res 3              ; (unused by asm; handy for C bookkeeping)
_band_y:    .res 3              ; sprite Y register value per band
_band_trig: .res 3              ; raster line at which to fire each band's IRQ
_band_x:    .res 24             ; [band*8 + i] sprite X (all < 256 by layout)
_band_ptr:  .res 24             ; [band*8 + i] sprite shape pointer (block #)
_band_col:  .res 24             ; [band*8 + i] sprite colour
_band_en:   .res 3              ; sprite-enable mask per band
band_idx:   .res 1              ; which band the *next* IRQ will program
tmpy:       .res 1              ; this band's Y value (cached for the unroll)
saved_irq:  .res 2              ; previous $0314/$0315 vector

.code

; void mux_install(void) -- take over IRQs with the raster multiplexer.
_mux_install:
        sei
        lda $0314
        sta saved_irq
        lda $0315
        sta saved_irq+1
        lda #<irq
        sta $0314
        lda #>irq
        sta $0315
        lda #$7f                ; disable all CIA1 IRQ sources (stop timer IRQ)
        sta CIA1_ICR
        lda CIA1_ICR            ; ack any pending CIA IRQ
        lda VIC_CTRL1           ; clear raster bit 8 (rasters all < 256)
        and #$7f
        sta VIC_CTRL1
        lda #0
        sta band_idx
        lda _band_trig          ; arm band 0's raster
        sta VIC_RASTER
        lda #$01                ; enable VIC raster IRQ
        sta VIC_IRQEN
        lda #$01                ; ack any pending raster IRQ
        sta VIC_IRQ
        cli
        rts

; void mux_stop(void) -- restore the normal KERNAL IRQ + CIA timer.
_mux_stop:
        sei
        lda #0
        sta VIC_IRQEN           ; disable VIC IRQ
        sta VIC_EN              ; all sprites off
        lda #$81                ; re-enable CIA1 timer-A IRQ
        sta CIA1_ICR
        lda saved_irq
        sta $0314
        lda saved_irq+1
        sta $0315
        cli
        rts

; ---- the multiplexer IRQ handler ------------------------------------------
; Entered via $0314 (A/X/Y already pushed by the KERNAL IRQ entry at $FF48).
irq:
        lda band_idx            ; base offset into the band tables = band*8
        asl
        asl
        asl
        tay                     ; Y walks band_x/ptr/col for the 8 sprites
        ldx band_idx
        lda _band_y,x           ; this band's sprite Y value
        sta tmpy

        ; --- sprite 0 ---
        lda _band_x,y
        sta VIC_SP0X+0
        lda tmpy
        sta VIC_SP0X+1
        lda _band_ptr,y
        sta SPRPTR+0
        lda _band_col,y
        sta VIC_SP0C+0
        iny
        ; --- sprite 1 ---
        lda _band_x,y
        sta VIC_SP0X+2
        lda tmpy
        sta VIC_SP0X+3
        lda _band_ptr,y
        sta SPRPTR+1
        lda _band_col,y
        sta VIC_SP0C+1
        iny
        ; --- sprite 2 ---
        lda _band_x,y
        sta VIC_SP0X+4
        lda tmpy
        sta VIC_SP0X+5
        lda _band_ptr,y
        sta SPRPTR+2
        lda _band_col,y
        sta VIC_SP0C+2
        iny
        ; --- sprite 3 ---
        lda _band_x,y
        sta VIC_SP0X+6
        lda tmpy
        sta VIC_SP0X+7
        lda _band_ptr,y
        sta SPRPTR+3
        lda _band_col,y
        sta VIC_SP0C+3
        iny
        ; --- sprite 4 ---
        lda _band_x,y
        sta VIC_SP0X+8
        lda tmpy
        sta VIC_SP0X+9
        lda _band_ptr,y
        sta SPRPTR+4
        lda _band_col,y
        sta VIC_SP0C+4
        iny
        ; --- sprite 5 ---
        lda _band_x,y
        sta VIC_SP0X+10
        lda tmpy
        sta VIC_SP0X+11
        lda _band_ptr,y
        sta SPRPTR+5
        lda _band_col,y
        sta VIC_SP0C+5
        iny
        ; --- sprite 6 ---
        lda _band_x,y
        sta VIC_SP0X+12
        lda tmpy
        sta VIC_SP0X+13
        lda _band_ptr,y
        sta SPRPTR+6
        lda _band_col,y
        sta VIC_SP0C+6
        iny
        ; --- sprite 7 ---
        lda _band_x,y
        sta VIC_SP0X+14
        lda tmpy
        sta VIC_SP0X+15
        lda _band_ptr,y
        sta SPRPTR+7
        lda _band_col,y
        sta VIC_SP0C+7

        ldx band_idx
        lda _band_en,x          ; enable just this band's active sprites
        sta VIC_EN

        ; advance to the next band (wrap 2 -> 0)
        inx
        cpx #3
        bcc store_idx
        ldx #0
store_idx:
        stx band_idx
        lda _band_trig,x        ; arm the next band's raster line
        sta VIC_RASTER
        lda #$01                ; ack the raster IRQ
        sta VIC_IRQ

        lda band_idx            ; did we just program band 0? (now idx == 1)
        cmp #1
        bne not_top
        jmp KERN_IRQ            ; yes: run KERNAL IRQ (keyboard, clock) then RTI
not_top:
        jmp KERN_RTI            ; no: just restore A/X/Y + RTI

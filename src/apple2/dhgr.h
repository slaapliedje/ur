/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Apple II double-hi-res (DHGR) — 140x192 in 16 colours.
 *
 * DHGR interleaves AUXILIARY and MAIN memory: each scanline is 80 seven-bit byte
 * groups (aux0, main0, aux1, main1, ...) = 560 mono pixels; every 4 mono pixels is
 * one of 16 colours.  A solid colour is a 4-bit pattern repeated across the line;
 * because 4 (colour) and 7 (byte) are coprime, the byte values cycle every 4 byte
 * groups (28 mono px), so a fill writes a repeating 4-byte pattern, phased by the
 * group index.  Page 1 lives at main+aux $2000-$3FFF (apple2enh-hgr.cfg keeps the
 * program out of there).  We use MIXED mode: 4 text lines (80-col, page $0400 —
 * separate from $2000) carry the panel via conio.
 *
 * Coordinates: x is a byte GROUP 0..79 (each 7 mono px); y is a scanline 0..191.
 * Filling in whole groups keeps colours fringe-free (cells align to the pattern).
 */
#ifndef UR_APPLE2_DHGR_H
#define UR_APPLE2_DHGR_H

/* The 16 DHGR colours, as 4-bit nibble patterns (verified against MAME, see
 * dhgr.c). Names approximate the lo-res palette. */
extern const unsigned char dhgr_nib[16];
#define DH_BLACK  0
#define DH_BLUE   1     /* indices into dhgr_nib[] — resolved after calibration */
#define DH_GREEN  2
#define DH_WHITE  3
#define DH_GREY   4
#define DH_ORANGE 5
#define DH_BROWN  6
#define DH_CYAN   7

void dhgr_on(void);                 /* mixed DHGR, page 2 */
void dhgr_off(void);                /* back to 40-col text */
/* Fill byte groups [g0..g1] x scanlines [y0..y1] with colour nibble `nib`. */
void dhgr_fill(unsigned char g0, unsigned char g1,
               unsigned char y0, unsigned char y1, unsigned char nib);
/* Panel text on the 4 mixed-mode lines (page-2 text, mirrored to aux). */
void dhgr_text(unsigned char col, unsigned char textrow, const char *s);
void dhgr_clrrow(unsigned char textrow);

#endif /* UR_APPLE2_DHGR_H */

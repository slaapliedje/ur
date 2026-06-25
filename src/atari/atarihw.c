/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Atari colour + POKEY sound helpers. See atarihw.h. */

#include "atarihw.h"

/* POKEY audio registers (channel 1). */
#define AUDF1  (*(volatile unsigned char *)0xD200)   /* frequency (divisor)   */
#define AUDC1  (*(volatile unsigned char *)0xD201)   /* distortion + volume   */
#define AUDCTL (*(volatile unsigned char *)0xD208)   /* audio control         */
#define WSYNC  (*(volatile unsigned char *)0xD40A)   /* wait for horiz. sync  */

/* OS colour shadow registers (copied to the hardware each vertical blank). */
#define COLOR1 (*(volatile unsigned char *)0x02C5)   /* GR.0 text luminance   */
#define COLOR2 (*(volatile unsigned char *)0x02C6)   /* GR.0 background        */
#define COLOR4 (*(volatile unsigned char *)0x02C8)   /* border                 */

#define PURE_TONE  0xA0   /* distortion bits for a clean tone (OR in volume 0-15) */
#define BUZZ_TONE  0x40   /* buzzy distortion (for capture)                       */

/* Busy-wait `lines` scanlines via WSYNC. The volatile store is never optimised
 * away, so this gives a reliable, audible duration. ~15700 scanlines ~= 1 second. */
static void delay_lines(unsigned int lines)
{
    unsigned int i;
    for (i = 0; i < lines; i++)
        WSYNC = 0;
}

static void tone(unsigned char pitch, unsigned char vol, unsigned int lines)
{
    AUDCTL = 0;
    AUDF1  = pitch;
    AUDC1  = (unsigned char)(PURE_TONE | (vol & 0x0F));
    delay_lines(lines);
    AUDC1  = 0;
}

static void buzz(unsigned char pitch, unsigned char vol, unsigned int lines)
{
    AUDCTL = 0;
    AUDF1  = pitch;
    AUDC1  = (unsigned char)(BUZZ_TONE | (vol & 0x0F));
    delay_lines(lines);
    AUDC1  = 0;
}

void atari_setup_colors(void)
{
    COLOR2 = 0x92;   /* background: blue                */
    COLOR1 = 0x0C;   /* text: bright (luminance)        */
    COLOR4 = 0x00;   /* border: black                   */
}

/* Lower AUDF1 = higher pitch (it's a divisor). Durations in scanlines. */
void sfx_roll(void)    { tone(96, 8, 500);  tone(72, 8, 500); }
void sfx_move(void)    { tone(80, 6, 600); }
void sfx_capture(void) { buzz(200, 12, 2400); }
void sfx_rosette(void) { tone(64, 8, 500);  tone(48, 8, 900); }
void sfx_score(void)   { tone(72, 8, 400);  tone(50, 8, 400);  tone(36, 8, 600); }
void sfx_win(void)
{
    tone(121, 8, 700);
    tone(96,  8, 700);
    tone(72,  8, 700);
    tone(60,  8, 1200);
}

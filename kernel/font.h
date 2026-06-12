/* =============================================================================
 * RedPandaOS - bitmap font
 * =============================================================================
 * The entry stub copied the VGA ROM's 8x16 font to low memory before leaving
 * real mode. One glyph = 16 bytes, one byte per row, MSB = leftmost pixel.
 * ============================================================================= */

#ifndef FONT_HEADER_H
#define FONT_HEADER_H

#include <stdint.h>

#define FONT_W 8
#define FONT_H 16
#define FONT_ADDR 0x6800     /* must match FONT_BUF in entry.asm */

static inline const uint8_t *font_glyph(unsigned char c)
{
    return (const uint8_t *)FONT_ADDR + (uint32_t)c * FONT_H;
}

#endif

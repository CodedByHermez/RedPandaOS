/* =============================================================================
 * RedPandaOS - video mode info
 * =============================================================================
 * The entry stub negotiated a VBE linear-framebuffer mode with the BIOS and
 * left the details at a fixed low-memory address (layout must match the
 * VIDEO_INFO stores in entry.asm).
 * ============================================================================= */

#ifndef VIDEOINFO_H
#define VIDEOINFO_H

#include <stdint.h>

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t bpp;        /* bits per pixel: 32 or 24 */
    uint16_t _pad;
    uint32_t pitch;      /* bytes per scanline (may exceed width * bpp/8) */
    uint32_t lfb;        /* physical address of the linear framebuffer */
} __attribute__((packed)) video_info_t;

static inline const video_info_t *video_info(void)
{
    return (const video_info_t *)0x6400;
}

#endif

#include "rtc.h"

#include <stdint.h>

#include "io.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int from_bcd(uint8_t v)
{
    return (v & 0x0F) + (v >> 4) * 10;
}

void rtc_time(int *hours, int *minutes, int *seconds)
{
    /* Wait until no update is in progress so we don't read a torn time. */
    for (int i = 0; i < 100000; i++)
        if (!(cmos_read(0x0A) & 0x80))
            break;

    uint8_t ss = cmos_read(0x00);
    uint8_t mm = cmos_read(0x02);
    uint8_t hh = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);

    int pm = hh & 0x80;         /* only meaningful in 12-hour mode */
    hh &= 0x7F;

    int h, m, s;
    if (!(status_b & 0x04)) {   /* BCD encoding (the default) */
        s = from_bcd(ss);
        m = from_bcd(mm);
        h = from_bcd(hh);
    } else {
        s = ss;
        m = mm;
        h = hh;
    }

    if (!(status_b & 0x02)) {   /* 12-hour mode -> convert to 24 */
        if (pm)
            h = (h % 12) + 12;
        else
            h = h % 12;
    }

    *hours = h;
    *minutes = m;
    *seconds = s;
}

/* =============================================================================
 * RedPandaOS - CMOS real-time clock
 * ============================================================================= */

#ifndef RTC_H
#define RTC_H

/* Read the current wall-clock time from the CMOS RTC. */
void rtc_time(int *hours, int *minutes, int *seconds);

#endif

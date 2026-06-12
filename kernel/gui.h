/* =============================================================================
 * RedPandaOS - the GUI
 * ============================================================================= */

#ifndef GUI_H
#define GUI_H

/* Run the desktop: wallpaper, taskbar, windows, event loop.
 * Returns only on the developer key (F12) - users stay in the GUI. */
void gui_run(void);

/* Open the Welcome + System windows (dev/testing; the start menu will be
 * the real launcher later). */
void gui_demo_windows(void);

#endif

/**
 * SH display
 *
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <shveu/shveu.h>

/**
 * An opaque handle to the display.
 */
struct DISPLAY;
typedef struct DISPLAY DISPLAY;

/**
 * Open the display
 * \retval 0 Failure
 * \retval >0 Handle
 */
DISPLAY *display_open(void);

/**
 * Close the frame buffer
 * \param disp Handle returned from display_open
 */
void display_close(DISPLAY *disp);

/**
 * Get the Renesas format of the display
 * \param disp Handle returned from display_open
 */
int display_get_format(DISPLAY *disp);

/**
 * Get the width of the display in pixels
 * \param disp Handle returned from display_open
 */
int display_get_width(DISPLAY *disp);

/**
 * Get the height of the display in pixels
 * \param disp Handle returned from display_open
 */
int display_get_height(DISPLAY *disp);

/**
 * Get a pointer to the back buffer
 * \param disp Handle returned from display_open
 */
unsigned char *display_get_back_buff(DISPLAY *disp);

/**
 * Place the back buffer on the screen
 * \param disp Handle returned from display_open
 */
int display_flip(DISPLAY *disp);



/* The functions below are used to place an image on the display */

/**
 * Position the output fullscreen (but observe aspect ratio)
 * \param disp Handle returned from display_open
 */
void display_set_fullscreen(DISPLAY *disp);

/**
 * Explicity position the output
 * \param disp Handle returned from display_open
 */
void display_set_position(DISPLAY *disp, int w, int h, int x, int y);

/**
 * Send frame to the framebuffer
 * \param disp Handle returned from display_open
 * \param src Surface to display
 */
int display_update(
	DISPLAY *disp,
	struct ren_vid_surface *src);

#endif


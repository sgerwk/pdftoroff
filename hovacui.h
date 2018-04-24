/*
 * hovacui.h
 */

#ifdef _HOVACUI_H
#else
#define _HOVACUI_H

/*
 * the options parse by hovacui itself
 */
#define HOVACUIOPTS "m:f:w:t:o:d:s:h"

/*
 * include curses to get the macro keys
 */
#include <curses.h>

/*
 * imaginary keys
 */
#define KEY_NONE	((KEY_MAX) + 1)
#define KEY_INIT	((KEY_MAX) + 2)
#ifdef KEY_REFRESH
#else
#define KEY_REFRESH	((KEY_MAX) + 3)
#endif
#define KEY_REDRAW	((KEY_MAX) + 4)
#ifdef KEY_RESIZE
#else
#define KEY_RESIZE	((KEY_MAX) + 5)
#endif
#define KEY_TIMEOUT	((KEY_MAX) + 6)
#ifdef KEY_SUSPEND
#else
#define KEY_SUSPEND	((KEY_MAX) + 7)
#endif
#define KEY_SIGNAL	((KEY_MAX) + 8)

/*
 * a cairo device
 */
struct cairooutput;
struct cairodevice {
	void *initdata;
	struct cairooutput *(*init)(char *device, void *initdata);
	void (*finish)(struct cairooutput *cairo);
	cairo_t *(*context)(struct cairooutput *cairo);
	double (*width)(struct cairooutput *cairo);
	double (*height)(struct cairooutput *cairo);
	double (*screenwidth)(struct cairooutput *cairo);
	double (*screenheight)(struct cairooutput *cairo);
	void (*clear)(struct cairooutput *cairo);
	void (*flush)(struct cairooutput *cairo);
	int (*input)(struct cairooutput *cairo, int timeout);
};

/*
 * show a pdf file on an arbitrary cairo device
 */
int hovacui(int argn, char *argv[], struct cairodevice *cairodevice);

#endif


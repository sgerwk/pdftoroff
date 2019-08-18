/*
 * cairoio.h
 */

#ifdef _CAIROOUTPUT_H
#else
#define _CAIROOUTPUT_H

#include <cairo.h>

/*
 * include curses to get the key macros
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
#define KEY_EXTERNAL	((KEY_MAX) + 9)
#define KEY_PASTE       ((KEY_MAX) + 10)

#define ISIMAGINARYKEY(c) (				   \
	((c) == KEY_NONE)				|| \
	((c) == KEY_INIT)				|| \
	((c) == KEY_REFRESH)				|| \
	((c) == KEY_REDRAW)				|| \
	((c) == KEY_RESIZE)				|| \
	((c) == KEY_TIMEOUT)				|| \
	((c) == KEY_SUSPEND)				|| \
	((c) == KEY_SIGNAL)				|| \
	((c) == KEY_EXTERNAL)				|| \
	((c) == KEY_PASTE)				   \
)
#define ISREALKEY(c) (! ISIMAGINARYKEY(c))

/*
 * no timeout
 */
#define NO_TIMEOUT (-1)

/*
 * external command
 */
struct command {
	int fd;
	FILE *stream;
	char *command;
	int max;
};

/*
 * a cairo device
 */
struct cairodevice {
	void *initdata;
	struct cairoio *(*init)(char *device,
		int doublebuffering, void *initdata);
	void (*finish)(struct cairoio *cairo);
	cairo_t *(*context)(struct cairoio *cairo);
	double (*width)(struct cairoio *cairo);
	double (*height)(struct cairoio *cairo);
	double (*screenwidth)(struct cairoio *cairo);
	double (*screenheight)(struct cairoio *cairo);
	void (*clear)(struct cairoio *cairo);
	void (*flush)(struct cairoio *cairo);
	int (*isactive)(struct cairoio *cairo);
	int (*input)(struct cairoio *cairo, int timeout,
	             struct command *command);
};

#endif


/*
 * cairoio.h
 */

/*
 * the cairodevice
 *
 * the cairodevice structure allows the cairoui interface to be run whenever a
 * cairo context can be obtained and input is available
 *
 * void init(struct cairodevice *cairodevice,
 *		char *device, int doublebuffering,
 *		int argn, char *argv[], char *opts);
 *	create the cairo context
 *
 * void finish(struct cairodevice *cairodevice);
 *	undo what done by init
 *
 * cairo_t *context(struct cairodevice *cairodevice);
 * double width(struct cairodevice *cairodevice);
 * double height(struct cairodevice *cairodevice);
 * double screenwidth(struct cairodevice *cairodevice);
 * double screenheight(struct cairodevice *cairodevice);
 *	return the cairo context and its size
 *
 * int doublebuffering(struct cairodevice *cairodevice);
 *	return whether double buffering is used
 *
 * void clear(struct cairodevice *cairodevice);
 * void blank(struct cairodevice *cairodevice);
 * void flush(struct cairodevice *cairodevice);
 *	clear and flush
 *
 * int isactive(struct cairodevice *cairodevice);
 *	whether the output is active
 *	do not draw on the framebuffer when the vt is switched out
 *
 * int input(struct cairodevice *cairodevice,
 *		int timeout, struct command *command);
 *	return a key
 *	on external command: store it in command->command, return KEY_EXTERNAL
 *	block for at most timeout milliseconds, NO_TIMEOUT=infinite
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
#define KEY_NONE	((KEY_MAX) +  1)
#define KEY_INIT	((KEY_MAX) +  2)
#define KEY_FINISH      ((KEY_MAX) +  3)
#ifdef KEY_REFRESH
#else
#define KEY_REFRESH	((KEY_MAX) +  4)
#endif
#define KEY_REDRAW	((KEY_MAX) +  5)
#ifdef KEY_RESIZE
#else
#define KEY_RESIZE	((KEY_MAX) +  6)
#endif
#define KEY_TIMEOUT	((KEY_MAX) +  7)
#ifdef KEY_SUSPEND
#else
#define KEY_SUSPEND	((KEY_MAX) +  8)
#endif
#define KEY_SIGNAL	((KEY_MAX) +  9)
#define KEY_EXTERNAL	((KEY_MAX) + 10)
#define KEY_PASTE       ((KEY_MAX) + 11)

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
	char *options;
	char *usage;
	struct cairoio *cairoio;
	int (*init)(struct cairodevice *cairodevice,
		char *device, int doublebuffering,
		int argn, char *argv[], char *opts);
	void (*finish)(struct cairodevice *cairodevice);
	cairo_t *(*context)(struct cairodevice *cairodevice);
	double (*width)(struct cairodevice *cairodevice);
	double (*height)(struct cairodevice *cairodevice);
	double (*screenwidth)(struct cairodevice *cairodevice);
	double (*screenheight)(struct cairodevice *cairodevice);
	int (*doublebuffering)(struct cairodevice *cairodevice);
	void (*clear)(struct cairodevice *cairodevice);
	void (*blank)(struct cairodevice *cairodevice);
	void (*flush)(struct cairodevice *cairodevice);
	int (*isactive)(struct cairodevice *cairodevice);
	int (*input)(struct cairodevice *cairodevice, int timeout,
	             struct command *command);
};

#endif


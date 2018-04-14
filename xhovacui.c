#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <ncurses.h>
#include "cairofb.h"
#include "hovacui.h"

/*
 * structure for a window
 */
struct xhovacui {
	Display *dsp;
	Window win;
};

/*
 * create a cairo context
 */
void *cairoinit(char *device) {
	printf("work in progress\n");
	printf("nothing to be seen here\n");
	return NULL;
}

/*
 * close a cairo context
 */
void cairofinish(void *cairo) {
}

/*
 * get the cairo context from a cairo envelope
 */
cairo_t *cairocontext(void *cairo) {
	return NULL;
}

/*
 * get the width from a cairo envelope
 */
double cairowidth(void *cairo) {
	return 0.0;
}

/*
 * get the heigth from a cairo envelope
 */
double cairoheight(void *cairo) {
	return 0.0;
}

/*
 * clear a cairo envelope
 */
void cairoclear(void *cairo) {
}

/*
 * flush a cairo envelope
 */
void cairoflush(void *cairo) {
}

/*
 * get a single input from a cairo envelope
 */
int cairoinput(void *cairo, int timeout) {
	/* translate X keys to curses */
	return KEY_TIMEOUT;
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	struct cairodevice cairodevice = {
		cairoinit, cairofinish,
		cairocontext, cairowidth, cairoheight,
		cairoclear, cairoflush, cairoinput
	};

	return hovacui(argn, argv, &cairodevice);
}


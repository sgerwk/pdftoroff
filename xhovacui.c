#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include<X11/keysym.h>
#include <ctype.h>
#include <ncurses.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include "cairofb.h"
#include "hovacui.h"

/*
 * todo:
 * - double buffering
 * - timeout: select on ConnectionNumber(xhovacui->dsp)
 * - use getenv("DISPLAY")
 */

/*
 * structure for a window
 */
struct xhovacui {
	cairo_surface_t *surface;
	cairo_t *cr;
	int width;
	int height;
	Display *dsp;
	Window win;
};

/*
 * create a cairo context
 */
void *cairoinit(char *device) {
	struct xhovacui *xhovacui;
	Screen *scr;
	Visual *vis;

	(void) device;

	xhovacui = malloc(sizeof(struct xhovacui));
	xhovacui->dsp = XOpenDisplay(NULL);
	if (xhovacui->dsp == NULL) {
		printf("cannot open display\n");
		free(xhovacui);
		return NULL;
	}
	scr = DefaultScreenOfDisplay(xhovacui->dsp);
	vis = DefaultVisualOfScreen(scr);

	xhovacui->width = 400;
	xhovacui->height = 300;

	xhovacui->win = XCreateSimpleWindow(xhovacui->dsp,
		DefaultRootWindow(xhovacui->dsp),
		200, 200, xhovacui->width, xhovacui->height, 1,
		BlackPixelOfScreen(scr), WhitePixelOfScreen(scr));
	XSelectInput(xhovacui->dsp, xhovacui->win,
	             KeyPressMask | ExposureMask | StructureNotifyMask);
	XMapWindow(xhovacui->dsp, xhovacui->win);

	xhovacui->surface =
		cairo_xlib_surface_create(xhovacui->dsp, xhovacui->win, vis,
			xhovacui->width, xhovacui->height);
	xhovacui->cr = cairo_create(xhovacui->surface);

	return xhovacui;
}

/*
 * close a cairo context
 */
void cairofinish(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	XDestroyWindow(xhovacui->dsp, xhovacui->win);
	XCloseDisplay(xhovacui->dsp);
	cairo_destroy(xhovacui->cr);
	cairo_surface_destroy(xhovacui->surface);
	free(xhovacui);
}

/*
 * get the cairo context from a cairo envelope
 */
cairo_t *cairocontext(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->cr;
}

/*
 * get the width from a cairo envelope
 */
double cairowidth(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->width;
}

/*
 * get the heigth from a cairo envelope
 */
double cairoheight(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->height;
	return 0.0;
}

/*
 * clear a cairo envelope
 */
void cairoclear(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	XClearWindow(xhovacui->dsp, xhovacui->win);
}

/*
 * flush a cairo envelope
 */
void cairoflush(void *cairo) {
	(void) cairo;
}

/*
 * get a single input from a cairo envelope
 */
int cairoinput(void *cairo, int timeout) {
	struct xhovacui *xhovacui;
	XEvent evt;
	XConfigureEvent *xce;
	int key;

	(void) timeout;

	xhovacui = (struct xhovacui *) cairo;

	/* to be done: use ConnectionManager(xhovacui->dsp) in a select()
	   with the given timeout, like in fbhovacui.c */
	XNextEvent(xhovacui->dsp, &evt);

	switch(evt.type) {
	case KeyPress:
		key = XLookupKeysym(&evt.xkey, 0);
		switch (key) {
		case XK_Down:
			return KEY_DOWN;
		case XK_Up:
			return KEY_UP;
		case XK_Left:
			return KEY_LEFT;
		case XK_Right:
			return KEY_RIGHT;
		case XK_Page_Down:
			return KEY_NPAGE;
		case XK_Page_Up:
			return KEY_PPAGE;
		case XK_Escape:
			return 033;
		case XK_Home:
			return KEY_HOME;
		case XK_End:
			return KEY_END;
		case XK_Return:
			return '\n';
		case XK_BackSpace:
			return KEY_BACKSPACE;
		case XK_slash:
			return '/';
		default:
			if (isalnum(key))
				return key;
		/* finish: translate X keys to curses */
		}
		break;
	case ConfigureNotify:
		xce = &evt.xconfigure;
		xhovacui->width = xce->width;
		xhovacui->height = xce->height;
		cairo_xlib_surface_set_size(xhovacui->surface,
			xhovacui->width, xhovacui->height);
		/* fallthrough */
	case Expose:
		return KEY_REDRAW;
		break;
	}

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


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
 * - event of type 65?
 * - timeout: select on ConnectionNumber(xhovacui->dsp)
 * - use getenv("DISPLAY") and parameter, but default for x11 should be :0.0
 *   and not /dev/fb0
 * - make default font size dependent on screen size
 * - ignore aspect? or use screen size instead of window size
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
	Pixmap dbuf;
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

	xhovacui->dbuf = XCreatePixmap(xhovacui->dsp, xhovacui->win,
		xhovacui->width, xhovacui->height,
		DefaultDepth(xhovacui->dsp, 0));
	xhovacui->surface =
		cairo_xlib_surface_create(xhovacui->dsp, xhovacui->dbuf, vis,
			xhovacui->width, xhovacui->height);
	xhovacui->cr = cairo_create(xhovacui->surface);

	XMapWindow(xhovacui->dsp, xhovacui->win);

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
}

/*
 * clear a cairo envelope
 */
void cairoclear(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	cairo_identity_matrix(xhovacui->cr);
	cairo_set_source_rgb(xhovacui->cr, 1.0, 1.0, 1.0);
	cairo_rectangle(xhovacui->cr, 0, 0, xhovacui->width, xhovacui->height);
	cairo_fill(xhovacui->cr);
}

/*
 * flush a cairo envelope
 */
void cairoflush(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	XCopyArea(xhovacui->dsp, xhovacui->dbuf, xhovacui->win,
		DefaultGC(xhovacui->dsp, 0),
		0, 0, xhovacui->width, xhovacui->height, 0, 0);
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
		printf("Key\n");
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
		printf("Configure\n");
		xce = &evt.xconfigure;
		xhovacui->width = xce->width;
		xhovacui->height = xce->height;

		XFreePixmap(xhovacui->dsp, xhovacui->dbuf);
		cairo_destroy(xhovacui->cr);
		cairo_surface_destroy(xhovacui->surface);

		xhovacui->dbuf = XCreatePixmap(xhovacui->dsp, xhovacui->win,
			xhovacui->width, xhovacui->height,
			DefaultDepth(xhovacui->dsp, 0));
		xhovacui->surface = cairo_xlib_surface_create(xhovacui->dsp,
				xhovacui->dbuf,
				DefaultVisual(xhovacui->dsp, 0),
				xhovacui->width, xhovacui->height);
		xhovacui->cr = cairo_create(xhovacui->surface);
		return KEY_RESIZE;
	case Expose:
		printf("Expose\n");
		return KEY_REDRAW;
	case GraphicsExpose:
		printf("GraphicsExpose\n");
		break;
	case NoExpose:
		printf("NoExpose\n");
		break;
	case MapNotify:
		printf("NapNotify\n");
		break;
	case ReparentNotify:
		printf("ReparentNotify\n");
		break;
	default:
		printf("event of type %d\n", evt.type);
	}

	return KEY_NONE;
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


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <ncurses.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include "cairofb.h"
#include "hovacui.h"

/*
 * todo:
 * - "hovacui: document.pdf" as the x11 window title
 * - turn option -display into -d
 */

/*
 * structure for a window
 */
struct xhovacui {
	cairo_surface_t *surface;
	cairo_t *cr;
	int width;
	int height;
	int screenwidth;
	int screenheight;
	Display *dsp;
	Window win;
	Pixmap dbuf;
};

/*
 * events we want to receive
 */
#define EVENTMASK (KeyPressMask | ExposureMask | StructureNotifyMask)

/*
 * create a cairo context
 */
void *cairoinit_x11(char *device, void *data) {
	struct xhovacui *xhovacui;
	Screen *scr;
	Visual *vis;
	char *title, *filename;

	xhovacui = malloc(sizeof(struct xhovacui));
	xhovacui->dsp = XOpenDisplay(device);
	if (xhovacui->dsp == NULL) {
		printf("cannot open display %s\n",
			device == NULL ? getenv("DISPLAY") : device);
		free(xhovacui);
		return NULL;
	}
	scr = DefaultScreenOfDisplay(xhovacui->dsp);
	vis = DefaultVisualOfScreen(scr);

	xhovacui->width = 600;
	xhovacui->height = 400;

	xhovacui->screenwidth = WidthOfScreen(scr);
	xhovacui->screenheight = HeightOfScreen(scr);

	xhovacui->win = XCreateSimpleWindow(xhovacui->dsp,
		DefaultRootWindow(xhovacui->dsp),
		200, 200, xhovacui->width, xhovacui->height, 1,
		BlackPixelOfScreen(scr), WhitePixelOfScreen(scr));
	XSelectInput(xhovacui->dsp, xhovacui->win, EVENTMASK);

	xhovacui->dbuf = XCreatePixmap(xhovacui->dsp, xhovacui->win,
		xhovacui->width, xhovacui->height,
		DefaultDepth(xhovacui->dsp, 0));
	xhovacui->surface =
		cairo_xlib_surface_create(xhovacui->dsp, xhovacui->dbuf, vis,
			xhovacui->width, xhovacui->height);
	xhovacui->cr = cairo_create(xhovacui->surface);

	filename = (char *) data;
	title = malloc(strlen("hovacui: ") + strlen(filename) + 1);
	strcpy(title, "hovacui: ");
	strcat(title, filename);
	XStoreName(xhovacui->dsp, xhovacui->win, title);

	XMapWindow(xhovacui->dsp, xhovacui->win);

	return xhovacui;
}

/*
 * close a cairo context
 */
void cairofinish_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	if (xhovacui == NULL)
		return;
	XDestroyWindow(xhovacui->dsp, xhovacui->win);
	XCloseDisplay(xhovacui->dsp);
	cairo_destroy(xhovacui->cr);
	cairo_surface_destroy(xhovacui->surface);
	free(xhovacui);
}

/*
 * get the cairo context
 */
cairo_t *cairocontext_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->cr;
}

/*
 * get the width of the window
 */
double cairowidth_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->width;
}

/*
 * get the heigth of the window
 */
double cairoheight_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->height;
}

/*
 * get the width of the screen
 */
double cairoscreenwidth_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->screenwidth;
}

/*
 * get the heigth of the screen
 */
double cairoscreenheight_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	return xhovacui->screenheight;
}

/*
 * clear
 */
void cairoclear_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	cairo_identity_matrix(xhovacui->cr);
	cairo_set_source_rgb(xhovacui->cr, 1.0, 1.0, 1.0);
	cairo_rectangle(xhovacui->cr, 0, 0, xhovacui->width, xhovacui->height);
	cairo_fill(xhovacui->cr);
}

/*
 * flush
 */
void cairoflush_x11(void *cairo) {
	struct xhovacui *xhovacui;
	xhovacui = (struct xhovacui *) cairo;
	XCopyArea(xhovacui->dsp, xhovacui->dbuf, xhovacui->win,
		DefaultGC(xhovacui->dsp, 0),
		0, 0, xhovacui->width, xhovacui->height, 0, 0);
}

/*
 * reconfigure
 */
void cairoreconfigure(struct xhovacui *xhovacui, XConfigureEvent *xce) {
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
}

/*
 * all available expose events
 */
int cairoexpose(Display *dsp, XEvent *evt) {
	XExposeEvent *exp;
	int redraw;

	do {
		redraw = 0;
		switch (evt->type) {
		case Expose:
			exp = &evt->xexpose;
			printf("Expose %d,%d->%dx%d\n",
				exp->x, exp->y,
				exp->width, exp->height);
			redraw = 1;
			break;
		case GraphicsExpose:
			printf("GraphicsExpose\n");
			break;
		case NoExpose:
			printf("NoExpose\n");
			break;
		default:
			printf("event of type %d\n", evt->type);
		}
	}
	while (XCheckMaskEvent(dsp, ExposureMask, evt));

	printf("\tend Exposure, redraw=%d\n", redraw);
	return redraw;
}

/*
 * next event or timeout
 */
int nextevent(Display *dsp, int timeout, XEvent *evt) {
	fd_set fds;
	int max, ret;
	struct timeval tv;

	/* the socket may be inactive but an event is already in the queue;
	 * so: first check events, then possibly wait on the socket */
	while (! XCheckMaskEvent(dsp, EVENTMASK, evt)) {
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dsp), &fds);
		max = ConnectionNumber(dsp);

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = timeout % 1000;

		ret = select(max + 1, &fds, NULL, NULL, timeout != 0 ? &tv : NULL);
		if (ret == -1)
			return -2;

		if (! FD_ISSET(ConnectionNumber(dsp), &fds))
			return -1;
	}

	return 0;
}

/*
 * get a single input
 */
int cairoinput_x11(void *cairo, int timeout) {
	struct xhovacui *xhovacui;
	XEvent evt;
	int key;

	xhovacui = (struct xhovacui *) cairo;

	while (1) {
		if (nextevent(xhovacui->dsp, timeout, &evt))
			return KEY_TIMEOUT;

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
			cairoreconfigure(xhovacui, &evt.xconfigure);
			return KEY_RESIZE;
		case Expose:
		case GraphicsExpose:
		case NoExpose:
			if (cairoexpose(xhovacui->dsp, &evt))
				return KEY_REDRAW;
			break;
		case MapNotify:
			printf("MapNotify\n");
			break;
		case ReparentNotify:
			printf("ReparentNotify\n");
			break;
		default:
			printf("event of type %d\n", evt.type);
		}
	}

	return KEY_NONE;
}

/*
 * the cairo device for X11
 */
struct cairodevice cairodevicex11 = {
	NULL,
	cairoinit_x11, cairofinish_x11,
	cairocontext_x11,
	cairowidth_x11, cairoheight_x11,
	cairoscreenwidth_x11, cairoscreenheight_x11,
	cairoclear_x11, cairoflush_x11, cairoinput_x11
};

/*
 * set window title
 */
void setx11title(int argn, char *argv[], struct cairodevice *cairodevice) {
	int opt;

	while (-1 != (opt = getopt(argn, argv, HOVACUIOPTS))) {
	}
	cairodevice->initdata = argv[optind];
}

/*
 * main
 */
#ifdef NOMAIN
#else
int main(int argn, char *argv[]) {
	setx11title(argn, argv, &cairodevicex11);
	return hovacui(argn, argv, &cairodevicex11);
}
#endif

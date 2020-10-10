#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <ctype.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include "cairoio.h"

/*
 * structure for a window
 */
struct cairoio {
	cairo_surface_t *surface;
	cairo_t *cr;
	unsigned int width;
	unsigned int height;
	int screenwidth;
	int screenheight;
	Display *dsp;
	Window win;
	Drawable dbuf;
	int doublebuffering;
};

/*
 * events we want to receive
 */
#define EVENTMASK \
	(KeyPressMask | ButtonPressMask | PropertyChangeMask | \
	ExposureMask | StructureNotifyMask)

/*
 * maximal length of pasted text; must be shorter than command->command
 */
#define MAXPASTE 200

/*
 * check whether b is a prefix of a
 */
int prefix(char *a, char *b) {
	return strncmp(a, b, strlen(b));
}

/*
 * extract the last part of a string
 */
char *second(char *a) {
	char *p;
	p = index(a, '=');
	return p == NULL ? p : p + 1;
}

/*
 * create a cairo context
 */
int cairoinit_x11(struct cairodevice *cairodevice,
		char *device, int doublebuffering,
		int argn, char *argv[], char *allopts) {
	int opt;
	struct cairoio *xhovacui;
	char *display;
	char *geometry;
	char *title;
	Screen *scr;
	Visual *vis;
	int x, y;
	char *wintitle;
	Atom utf8, name, pid;
	int pidn;

	display = NULL;
	geometry = NULL;
	optind = 1;
	while (-1 != (opt = getopt(argn, argv, allopts))) {
		switch (opt) {
		case 'x':
			if (! strcmp(optarg, "default"))
				continue;
			else if (! prefix(optarg, "display="))
				display = second(optarg);
			else if (! prefix(optarg, "geometry="))
				geometry = second(optarg);
			else {
				printf("unknown -x suboption: %s\n", optarg);
				return -1;
			}
			break;
		}
	}
	title = argv[optind];

	if (display != NULL)
		device = display;

	xhovacui = malloc(sizeof(struct cairoio));
	xhovacui->dsp = XOpenDisplay(device);
	if (xhovacui->dsp == NULL) {
		printf("cannot open display %s\n",
			device == NULL ? getenv("DISPLAY") : device);
		free(xhovacui);
		return -1;
	}
	scr = DefaultScreenOfDisplay(xhovacui->dsp);
	vis = DefaultVisualOfScreen(scr);

	x = 200;
	y = 200;
	xhovacui->width = 600;
	xhovacui->height = 400;
	if (geometry != NULL)
		XParseGeometry(geometry,
			&x, &y, &xhovacui->width, &xhovacui->height);
	printf("geometry: %dx%d+%d+%d\n",
		xhovacui->width, xhovacui->height, x, y);

	xhovacui->screenwidth = WidthOfScreen(scr);
	xhovacui->screenheight = HeightOfScreen(scr);

	xhovacui->win = XCreateSimpleWindow(xhovacui->dsp,
		DefaultRootWindow(xhovacui->dsp),
		x, y, xhovacui->width, xhovacui->height, 0,
		BlackPixelOfScreen(scr), WhitePixelOfScreen(scr));
	XSelectInput(xhovacui->dsp, xhovacui->win, EVENTMASK);

	xhovacui->doublebuffering = doublebuffering;
	xhovacui->dbuf = ! xhovacui->doublebuffering ?
		xhovacui->win :
		XCreatePixmap(xhovacui->dsp, xhovacui->win,
			xhovacui->width, xhovacui->height,
			DefaultDepth(xhovacui->dsp, 0));
	xhovacui->surface =
		cairo_xlib_surface_create(xhovacui->dsp, xhovacui->dbuf, vis,
			xhovacui->width, xhovacui->height);
	xhovacui->cr = cairo_create(xhovacui->surface);

	wintitle = malloc(strlen("hovacui: ") + strlen(title) + 1);
	strcpy(wintitle, "hovacui: ");
	strcat(wintitle, title);
	XStoreName(xhovacui->dsp, xhovacui->win, wintitle);
	utf8 = XInternAtom(xhovacui->dsp, "UTF8_STRING", False);
	name = XInternAtom(xhovacui->dsp, "_NET_WM_NAME", False);
	XChangeProperty(xhovacui->dsp, xhovacui->win,
		name, utf8, 8, PropModeReplace,
		(unsigned char *) wintitle, strlen(wintitle));
	pid = XInternAtom(xhovacui->dsp, "_NET_WM_PID", False);
	pidn = getpid();
	XChangeProperty(xhovacui->dsp, xhovacui->win,
		pid, XA_CARDINAL, 32, PropModeReplace,
		(unsigned char *) &pidn, 1);
	free(wintitle);

	XMapWindow(xhovacui->dsp, xhovacui->win);

	cairodevice->cairoio = xhovacui;
	return 0;
}

/*
 * close a cairo context
 */
void cairofinish_x11(struct cairodevice *cairodevice) {
	struct cairoio *xhovacui;
	xhovacui = cairodevice->cairoio;
	if (xhovacui == NULL)
		return;
	cairo_destroy(xhovacui->cr);
	cairo_surface_destroy(xhovacui->surface);
	if (xhovacui->doublebuffering)
		XFreePixmap(xhovacui->dsp, xhovacui->dbuf);
	XDestroyWindow(xhovacui->dsp, xhovacui->win);
	XCloseDisplay(xhovacui->dsp);
	free(xhovacui);
}

/*
 * get the cairo context
 */
cairo_t *cairocontext_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->cr;
}

/*
 * get the width of the window
 */
double cairowidth_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->width;
}

/*
 * get the heigth of the window
 */
double cairoheight_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->height;
}

/*
 * get the width of the screen
 */
double cairoscreenwidth_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->screenwidth;
}

/*
 * get the heigth of the screen
 */
double cairoscreenheight_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->screenheight;
}

/*
 * return whether double buffering is used
 */
int cairodoublebuffering_x11(struct cairodevice *cairodevice) {
	return cairodevice->cairoio->doublebuffering;
}

/*
 * clear
 */
void cairoclear_x11(struct cairodevice *cairodevice) {
	struct cairoio *xhovacui;
	xhovacui = cairodevice->cairoio;
	cairo_identity_matrix(xhovacui->cr);
	cairo_set_source_rgb(xhovacui->cr, 1.0, 1.0, 1.0);
	cairo_rectangle(xhovacui->cr, 0, 0, xhovacui->width, xhovacui->height);
	cairo_fill(xhovacui->cr);
}

/*
 * blank
 */
void cairoblank_x11(struct cairodevice *cairodevice) {
	struct cairoio *xhovacui;
	xhovacui = cairodevice->cairoio;
	cairo_identity_matrix(xhovacui->cr);
	cairo_set_source_rgb(xhovacui->cr, 0.0, 0.0, 0.0);
	cairo_rectangle(xhovacui->cr, 0, 0, xhovacui->width, xhovacui->height);
	cairo_fill(xhovacui->cr);
}

/*
 * flush
 */
void cairoflush_x11(struct cairodevice *cairodevice) {
	struct cairoio *xhovacui;
	xhovacui = cairodevice->cairoio;
	if (xhovacui->doublebuffering)
		XCopyArea(xhovacui->dsp, xhovacui->dbuf, xhovacui->win,
			DefaultGC(xhovacui->dsp, 0),
			0, 0, xhovacui->width, xhovacui->height, 0, 0);
}

/*
 * whether the output is currently active
 */
int cairoisactive_x11(struct cairodevice *cairodevice) {
	(void) cairodevice;
	return TRUE;
}

/*
 * reconfigure
 */
void cairoreconfigure(struct cairoio *xhovacui, XConfigureEvent *xce) {
	xhovacui->width = xce->width;
	xhovacui->height = xce->height;

	if (! xhovacui->doublebuffering) {
		cairo_xlib_surface_set_size(xhovacui->surface,
			xhovacui->width, xhovacui->height);
		return;
	}

	XFreePixmap(xhovacui->dsp, xhovacui->dbuf);
	xhovacui->dbuf = XCreatePixmap(xhovacui->dsp, xhovacui->win,
		xhovacui->width, xhovacui->height,
		DefaultDepth(xhovacui->dsp, 0));
	cairo_xlib_surface_set_drawable(xhovacui->surface, xhovacui->dbuf,
		xhovacui->width, xhovacui->height);
	return;
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
int nextevent(Display *dsp, int timeout, XEvent *evt, struct command *command) {
	fd_set fds;
	int max, ret;
	struct timeval tv;

	/* the socket may be inactive but an event is already in the queue;
	 * so: first check events, then possibly wait on the socket */
	while (! XCheckMaskEvent(dsp, EVENTMASK, evt)) {
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dsp), &fds);
		max = ConnectionNumber(dsp);
		if (command->fd != -1) {
			FD_SET(command->fd, &fds);
			max = max > command->fd ? max : command->fd;
		}

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		ret = select(max + 1, &fds, NULL, NULL,
			timeout != NO_TIMEOUT ? &tv : NULL);
		if (ret == -1)
			return -1;

		if (command->fd != -1 && FD_ISSET(command->fd, &fds)) {
			fgets(command->command, command->max, command->stream);
			return KEY_EXTERNAL;
		}

		if (! FD_ISSET(ConnectionNumber(dsp), &fds))
			return KEY_TIMEOUT;
	}

	return 0;
}

/*
 * get a single input
 */
int cairoinput_x11(struct cairodevice *cairodevice, int timeout,
		struct command *command) {
	struct cairoio *xhovacui;
	int res;
	XEvent evt;
	int key;
	int format;
	Atom type;
	unsigned long nitems, after;
	unsigned char *selection;

	xhovacui = cairodevice->cairoio;

	while (1) {
		res = nextevent(xhovacui->dsp, timeout, &evt, command);
		if (res != 0)
			return res;

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
			case XK_space:
				return ' ';
			default:
				if (isalnum(key)) {
					if (evt.xkey.state & ShiftMask)
						return toupper(key);
					else
						return key;
				}
			/* finish: translate X keys to curses */
			}
			break;
		case ButtonPress:
			printf("Button\n");
			if (evt.xbutton.button == 2) {
				XConvertSelection(xhovacui->dsp,
					XA_PRIMARY, XA_STRING, XA_PRIMARY,
					xhovacui->win, CurrentTime);
			}
			break;
		case PropertyNotify:
			printf("Property\n");
			if (evt.xproperty.atom != XA_PRIMARY)
				break;
			res = XGetWindowProperty(xhovacui->dsp, xhovacui->win,
				XA_PRIMARY, 0, MAXPASTE, True, XA_STRING,
				&type, &format, &nitems, &after, &selection);
			if (res != Success)
				break;
			if (type != XA_STRING)
				break;
			if (nitems > MAXPASTE)
				break;
			if (format != 8)
				break;
			strcpy(command->command, (char *) selection);
			XFree(selection);
			return KEY_PASTE;
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
	"x:",
	"\t\t-x suboption\tx11 options (display, geometry)",
	NULL,
	cairoinit_x11, cairofinish_x11,
	cairocontext_x11,
	cairowidth_x11, cairoheight_x11,
	cairoscreenwidth_x11, cairoscreenheight_x11,
	cairodoublebuffering_x11,
	cairoclear_x11, cairoblank_x11, cairoflush_x11,
	cairoisactive_x11, cairoinput_x11
};


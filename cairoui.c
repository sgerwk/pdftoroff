/*
 * cairoui.c
 *
 * a simple graphical user interface based on cairo
 */

/*
 * todo
 *
 * - the main loop looks like a state machine; implement as such
 * - utf8 or widechar in cairodevice->input() and field()
 * - allow tabs in list(), make it display a table rather than a list;
 *   use in help() to separate keys from functions,
 *   in menu() to show the current values for viewmode, fit, etc.
 * - support 8bpp framebuffers (via a fixed colormap)
 * - add an optional help to the bottom of list()
 * - numbermode: 2 is a, 22 is b, 222 is c, etc.
 * - function to be possibly called before list() to wrap lines too long
 */

/*
 * the source files
 * ----------------
 *
 * cairoio.h
 * cairoio-fb.c
 * cairoio-x11.c
 *	input and output:
 *	- input is a function that gets the input keystrokes
 *	- output is a cairo context to draw onto and some functions
 *	  (clear, flush, etc.)
 *
 * cairoui.h
 * cairoui.c
 *	the graphics:
 *	- a scrollable list of strings (possibly with a selected element)
 *	- a textfield
 *	- the main loop
 */

/*
 * windows and labels
 * ------------------
 *
 * the graphical interface is made of windows and labels; windows receive
 * input, labels do not; both are functions with state stored as static
 * variables or fields in the callback structure cairoui->cb
 *
 * int window(int c, struct cairoui *cairoui);
 *	the given window is activated (if not already) and receive input c:
 *	- input is a key, but can also be KEY_INIT, KEY_REDRAW or KEY_REFRESH
 *	- output is the next window to become active, or CAIROUI_REFRESH
 *
 * void label(struct cairoio *cairoui);
 *	all label functions are called at each step; they have to decide by
 *	themselves whether to draw something; they choose based on the content
 *	of the callback structure cairoui->cb
 *
 * each is a specific instance of a generic template:
 *
 * field()
 *	a generic textfield
 *
 * number()
 *	a textfield for an integer number; calls field()
 *
 * list()
 *	a list of strings, possibly with a selected one
 *
 * label()
 *	a label shown on screen
 *
 * the templates draw and set cairoui->flush; a window preprocesses the input
 * character if necessary, then calls the template and processes its return
 * value, possibly using and changing the callback structure cairoui->cb; it
 * returns the next window, or CAIROUI_REFRESH to have the document be redrawn
 * and to be called again with input KEY_REFRESH
 *
 * a particular window is document(), which draws nothing and deals with input
 * when no other window is active; this is the only window that can set
 * cairoui->redraw to redraw the document
 *
 * the document is not redrawn at each step; rather, windows and labels are
 * just drawn over it, and therefore remain on the screen until the document is
 * redrawn; the document is redrawn only when:
 * - the input timeout was set >0 and it either expires or a key arrived
 * - the virtual terminal is switched in
 * - document() sets cairoui->redraw
 * - a window other than document() returns another window
 * - a window or external command returns CAIROUI_REFRESH
 *
 * a window requests probing the input by setting cairoui->timeout=0 and
 * returning itself; the document is redrawn only if cairoui->redraw is set;
 * instead, returning CAIROUI_REFRESH causes no input probe and always redraws
 * the document
 *
 * the list of windows is the array of structures cairoui->windowlist; each
 * structure contains: an integer identifying the window, a string for logging
 * the window and the window function; the first window of the array is always
 * document(); the list of labels is the array of functions cairoui->labellist
 */

/*
 * the callbacks
 * -------------
 *
 * all windows and labels access arbitrary data stored as the pointer-to-void
 * field cairoui->cb; besides windows and labels, the cairoui structure
 * contains four callback functions:
 *
 * void draw(struct cairoui *cairoui);
 *	draw the document
 *
 * void resize(struct cairoui *cairoui);
 *	called when the size of the window changes
 *
 * void update(struct cairoui *cairoui);
 *	called when cairoui->reload is true
 *
 * int external(struct cairoui *cairoui, int window);
 *	called when a command is received from the fifo cairoui->command.fd;
 *	the received command is cairoui->command.command
 */

/*
 * note: the main loop
 *
 * is a sequence of three steps, each skipped in certain situations
 *
 * 1. draw the document, call the label functions, flush
 *	nothing is done if the output is not active (vt switched out)
 *	drawing and flushing depends on cairoui->redraw and cairoui->flush
 *
 * 2. receive input
 *	actual input is read only if c == KEY_NONE
 *	input may be KEY_TIMEOUT, KEY_REDRAW, etc. (see below)
 *	in some cases this step ends with a "continue" to skip step 3
 *
 * 3. call the window function or the external command function
 *	draw the window
 *	set cairoui->redraw or cairoui->flush if necessary
 *
 * relevant variables:
 * c		if an input character is read, it is stored in this variable;
 *		special values KEY_* carry instructions for the next iteration
 *		such as skipping input and refreshing the screen instead, etc.
 * window	the current window
 * next		the next window, or CAIROUI_REFRESH
 * output.cairoui.redraw whether to redraw the document at the next step 1
 * output.cairoui.flush	whether to flush the document afterward
 *
 * example: change a page in hovacui by an external command while the main menu
 * is active; external commands are read in step 2; step 3 calls the external()
 * function, which changes the page number and returns CAIROUI_REFRESH; this
 * causes step 3 to set cairoui->redraw=TRUE, cairoui->flush=FALSE and
 * c=KEY_REFRESH; in step 1, cairoui->redraw=TRUE causes the document to be
 * redrawn, and output->redraw to be set to false; when pagelabel() label is
 * called it detects a page change and draws the page number; flushing is not
 * done because cairoui->flush=FALSE; step 2 skips reading input since c is not
 * KEY_NONE; step 3 calls the menu window with c=KEY_REFRESH, which causes it
 * to redraw itself and set cairoui->flush=TRUE; step 1 this time has
 * cairoui->redraw=FALSE and cairoui->flush=TRUE, and therefore flushes the
 * output
 */

/*
 * note: input
 *
 * a window is a function; its first argument is the user input, represented as
 * an integer; when the window is open, it is called on each keystroke, and may
 * also be called with one of the imaginary keys:
 *
 * KEY_NONE
 *	no window ever receives this
 *
 * KEY_INIT
 *	upon opening, the window is called the first time with this value;
 *	every input different than KEY_INIT indicate that the window has not
 *	been closed in the meantime
 *
 * KEY_FINISH
 *	just before moving to a new window, the current window receives this;
 *	it is useful when moving is requested by an external command and not by
 *	the window itself, and the window has to take some final action such as
 *	freeing memory or closing files; the return value of the window is
 *	ignored
 *
 * KEY_REDRAW
 *	used internally when the window has to redraw itself because of an
 *	external reason (the vt has been switched in, the x11 window has
 *	received an expose event); the window receives KEY_REFRESH
 *
 * KEY_REFRESH
 *	the window has to redraw itself; this may happen for external reasons
 *	(see KEY_REDRAW), but also when the window has requested a redraw of
 *	the document or the labels by returning CAIROUI_REFRESH
 *
 * KEY_RESIZE
 *	the space where the pdf is drawn has been resized
 *
 * KEY_TIMEOUT
 *	user input is normally waited with no time limit, but windows and
 *	labels may set a timeout in cairoui->timeout; this value tells that the
 *	timeout expired; windows generally ignore this value
 *
 * KEY_SUSPEND
 *	the program should not draw anything
 *
 * KEY_SIGNAL
 *	the input select() was interrupted by a signal; none of the windows use
 *	this value
 *
 * KEY_EXTERNAL
 *	an external command come from the input fifo; no window receives this
 */

/*
 * note: label initialization
 *
 * labels decide by themselves whether to draw (calling cairoui_label()) or
 * not; they receive the cairoui structure, which contains the cairoui->cb
 * callback data; the decision to draw may be based on the current values or
 * their change; the latter requires an initialization if the labels are not to
 * be shown at startup; this is done by cairoui_initlabels(), which calls the
 * labels with a tiny, temporary memory-based cairo context, so that drawing is
 * invisible; this function is called by the main loop itself if the first
 * window is not document()
 */

/*
 * note: paste
 *
 * paste is implemented by passing KEY_PASTE to the active window, with the
 * pasted text stored in cairoui->paste; the active window decides what to do
 * with this text: cairoui_field() includes it, the other windows ignore
 * it; more generally, only the windows waiting for a string use the pasted
 * text; the windows expecting input in form of a keystroke do not
 */

/*
 * note: the cairodevice
 *
 * the cairodevice structure allows the cairoui interface to be run whenever a
 * cairo context can be obtained and input is available; two such devices are
 * available: framebuffer+stdio and x11
 *
 * a cairoui program needs one such structure to:
 * - store its pointer in cairoui->cairodevice
 * - obtain the device-specific commdandline options cairodevice->options
 * - initialize and check the availability of the device: cairodevice->init()
 * - find the size of the whole screen in the cairui->resize() callback
 *
 * most of the work on the cairodevice is done by the main loop
 *
 * details in cairoio.h
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include "cairoio.h"
#include "cairoui.h"
#undef clear

/*
 * reload by signal
 */
int sig_reload;

/*
 * return value of windows when they finish
 */
int _cairoui_out(int res) {
	return res == CAIROUI_DONE ||
	       res == CAIROUI_LEAVE ||
	       res == CAIROUI_FAIL;
}

/*
 * time between equal keys
 */
time_t _interval_equal(int c) {
	static time_t prev = -1;
	static int d = KEY_NONE;
	time_t curr, diff;

	curr = time(NULL);
	diff = c == d ? curr - prev : 10000;
	prev = curr;
	d = c;
	return diff;
}

/*
 * a changeable rectangle
 */
int cairoui_rectangle(int c, struct cairoui *cairoui, int corner,
		cairo_rectangle_t *rect) {
	double x1, y1, x2, y2;
	double *x, *y;
	int step;

	x1 = rect->x;
	y1 = rect->y;
	x2 = rect->x + rect->width;
	y2 = rect->y + rect->height;

	x = corner == 0 ? &x1 : &x2;
	y = corner == 0 ? &y1 : &y2;

	if (c == KEY_INIT || c == KEY_REFRESH) {
		cairo_identity_matrix(cairoui->cr);
		cairo_set_source_rgb(cairoui->cr, 1.0, 0.0, 0.0);
		cairo_rectangle(cairoui->cr, *x - 5, *y - 5, 10, 10);
		cairo_fill(cairoui->cr);
		cairo_rectangle(cairoui->cr,
			rect->x, rect->y, rect->width, rect->height);
		cairo_stroke(cairoui->cr);
		cairoui->flush = TRUE;
		return CAIROUI_CHANGED;
	}

	step = _interval_equal(c) < 200 ? 25 : 10;

	switch (c) {
	case KEY_RIGHT:
		*x += step;
		break;
	case KEY_LEFT:
		*x -= step;
		break;
	case KEY_UP:
		*y -= step;
		break;
	case KEY_DOWN:
		*y += step;
		break;
	case 'c':
		break;
	case '\033':
	case KEY_EXIT:
	case KEY_FINISH:
		return CAIROUI_LEAVE;
	case KEY_ENTER:
	case '\n':
		return CAIROUI_DONE;
	default:
		return CAIROUI_UNCHANGED;
	}

	if (*x < cairoui->dest.x)
		*x = cairoui->dest.x;
	if (*x > cairoui->dest.x + cairoui->dest.width)
		*x = cairoui->dest.x + cairoui->dest.width;
	if (*y < cairoui->dest.y)
		*y = cairoui->dest.y;
	if (*y > cairoui->dest.y + cairoui->dest.height)
		*y = cairoui->dest.y + cairoui->dest.height;

	rect->x = x1;
	rect->y = y1;
	rect->width = x2 - x1;
	rect->height = y2 - y1;

	cairoui->redraw = TRUE;
	return CAIROUI_REFRESH;
}

/*
 * a list of strings, possibly with a selected one
 */
int cairoui_list(int c, struct cairoui *cairoui, char *viewtext[],
		int *line, int *selected) {
	double percent = 0.8;
	double width = cairoui->dest.width;
	double height = cairoui->dest.height;
	double x2 = cairoui->dest.x + width;
	double marginx = width * (1 - percent) / 2;
	double marginy = height * (1 - percent) / 2;
	double borderx = 10.0;
	double bordery = 10.0;
	double titleheight = cairoui->extents.height + 2 * bordery;
	double textheight, listheight;
	double startx = cairoui->dest.x + marginx;
	double starty = cairoui->dest.y + marginy;
	double startlist = starty + titleheight + bordery;
	int n, l, lines, next;

	for (n = 1; viewtext[n] != NULL; n++) {
	}

	cairo_identity_matrix(cairoui->cr);
	lines = (int) (height * percent - titleheight - bordery * 2) /
		(int) cairoui->extents.height;
	textheight = (n - 1 < lines ? n - 1 : lines) * cairoui->extents.height;
	listheight = textheight + 2 * bordery;
	height = textheight + listheight;

	switch (c) {
	case KEY_DOWN:
		if (selected != NULL) {
			next = *selected;
			do {
				next++;
			} while (next < n && viewtext[next][0] == '\0');
			if (next >= n)
				return CAIROUI_UNCHANGED;
			*selected = next;
			if (*selected >= *line + lines)
				*line = *selected - lines;
		}
		else if (*line >= n - lines - 1)
			return CAIROUI_UNCHANGED;
		else
			(*line)++;
		break;
	case KEY_UP:
		if (selected != NULL) {
			next = *selected;
			do {
				next--;
			} while (next >= 1 && viewtext[next][0] == '\0');
			if (next < 1)
				return CAIROUI_UNCHANGED;
			*selected = next;
			if (*selected <= *line)
				*line = *selected - 1;
		}
		else if (*line <= 0)
			return CAIROUI_UNCHANGED;
		else
			(*line)--;
		break;
	case KEY_INIT:
	case KEY_REDRAW:
	case KEY_RESIZE:
	case KEY_REFRESH:
		break;
	case '\033':
	case KEY_EXIT:
	case KEY_FINISH:
		return CAIROUI_LEAVE;
	case KEY_ENTER:
	case '\n':
		return selected != NULL ?
			CAIROUI_DONE : CAIROUI_LEAVE;
	default:
		return selected != NULL ?
			CAIROUI_UNCHANGED : CAIROUI_LEAVE;
	}

				/* list heading */

	cairo_set_source_rgb(cairoui->cr, 0.6, 0.6, 0.8);
	cairo_rectangle(cairoui->cr,
		startx, starty, width - marginx * 2, titleheight);
	cairo_fill(cairoui->cr);
	cairo_set_source_rgb(cairoui->cr, 0.0, 0.0, 0.0);
	cairo_move_to(cairoui->cr,
		startx + borderx, starty + bordery + cairoui->extents.ascent);
	cairo_show_text(cairoui->cr, viewtext[0]);

				/* clip to make the list scrollable */

	cairo_set_source_rgb(cairoui->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(cairoui->cr,
		startx, starty + titleheight, width - marginx * 2, listheight);
	cairo_fill(cairoui->cr);

				/* background */

	cairo_set_source_rgb(cairoui->cr, 0.0, 0.0, 0.0);
	cairo_save(cairoui->cr);
	cairo_rectangle(cairoui->cr,
		startx, startlist,
		width - marginx * 2, textheight);
	cairo_clip(cairoui->cr);

				/* draw the list elements */

	cairo_translate(cairoui->cr, 0.0, - cairoui->extents.height * *line);
	for (l = 1; viewtext[l] != NULL; l++) {
		if (selected == NULL || l != *selected)
			cairo_set_source_rgb(cairoui->cr, 0.0, 0.0, 0.0);
		else {
			cairo_set_source_rgb(cairoui->cr, 0.3, 0.3, 0.3);
			cairo_rectangle(cairoui->cr,
				startx,
				startlist + cairoui->extents.height * (l - 1),
				width - 2 * marginx,
				cairoui->extents.height);
			cairo_fill(cairoui->cr);
			cairo_set_source_rgb(cairoui->cr, 0.8, 0.8, 0.8);
		}
		cairo_move_to(cairoui->cr,
			startx + borderx,
			startlist + cairoui->extents.height * (l - 1) +
				cairoui->extents.ascent);
		cairo_show_text(cairoui->cr, viewtext[l]);
	}
	cairo_stroke(cairoui->cr);
	cairo_restore(cairoui->cr);

				/* draw the scrollbar */

	if (lines < n - 1) {
		cairo_rectangle(cairoui->cr,
			x2 - marginx - borderx,
			cairoui->dest.y + marginy + titleheight +
				*line / (double) (n - 1) * listheight,
			borderx,
			lines / (double) (n - 1) * listheight);
		cairo_fill(cairoui->cr);
		cairo_stroke(cairoui->cr);
	}

	cairoui->flush = TRUE;
	return CAIROUI_CHANGED;
}

/*
 * generic textfield
 */
int cairoui_field(int c, struct cairoui *cairoui,
		char *prompt, char *current, int *pos, char *error) {
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (cairoui->dest.width) * prop;
	double marginy = 20.0;
	double x2 = cairoui->dest.x + cairoui->dest.width;
	double startx = cairoui->dest.x + marginx;
	double starty = cairoui->dest.y + marginy;
	double x, y;
	cairo_text_extents_t te;
	char cursor;
	int len, plen, i;

	if (c == '\033' || c == KEY_EXIT || c == KEY_FINISH)
		return CAIROUI_LEAVE;
	if (c == '\n' || c == KEY_ENTER)
		return CAIROUI_DONE;

	len = strlen(current);
	if (c == KEY_BACKSPACE || c == KEY_DC) {
		if (*pos <= 0)
			return CAIROUI_UNCHANGED;
		for (i = *pos; i <= len; i++)
			current[i - 1] = current[i];
		(*pos)--;
	}
	else if (c == KEY_LEFT) {
		if (*pos <= 0)
			return CAIROUI_UNCHANGED;
		(*pos)--;
	}
	else if (c == KEY_RIGHT) {
		if (*pos >= 30 || *pos >= len)
			return CAIROUI_UNCHANGED;
		(*pos)++;
	}
	else if (c == KEY_PASTE) {
		plen = strlen(cairoui->paste);
		if (len + plen > 30)
			return CAIROUI_UNCHANGED;
		for (i = 0; i < plen; i++) {
			current[*pos + plen] = current[*pos];
			current[*pos] = cairoui->paste[i];
			(*pos)++;
		}
		current[len + plen] = '\0';
	}
	else if (ISREALKEY(c)) {
		if (len > 30)
			return CAIROUI_UNCHANGED;
		for (i = len + 1; i >= *pos; i--)
			current[i + 1] = current[i];
		current[*pos] = c;
		(*pos)++;
	}

	cairoui->flush = TRUE;

	cairo_identity_matrix(cairoui->cr);

	cairo_set_source_rgb(cairoui->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(cairoui->cr,
		startx,
		starty,
		cairoui->dest.width - marginx * 2,
		cairoui->extents.height + 10);
	cairo_fill(cairoui->cr);

	cairo_set_source_rgb(cairoui->cr, 0.0, 0.0, 0.0);
	cairo_move_to(cairoui->cr,
		startx + 10.0,
		starty + 5.0 + cairoui->extents.ascent);
	cairo_show_text(cairoui->cr, prompt);
	cursor = current[*pos];
	current[*pos] = '\0';
	cairo_show_text(cairoui->cr, current);
	cairo_get_current_point(cairoui->cr, &x, &y);
	cairo_show_text(cairoui->cr, "_");
	cairo_move_to(cairoui->cr, x, y);
	current[*pos] = cursor;
	cairo_show_text(cairoui->cr, current + *pos);
	if (error == NULL)
		return CAIROUI_CHANGED;
	cairo_text_extents(cairoui->cr, error, &te);
	cairo_set_source_rgb(cairoui->cr, 0.8, 0.0, 0.0);
	cairo_rectangle(cairoui->cr,
		x2 - marginx - te.x_advance - 20.0,
		starty,
		te.x_advance + 20.0,
		cairoui->extents.height + 10.0);
	cairo_fill(cairoui->cr);
	cairo_move_to(cairoui->cr,
		x2 - marginx - te.x_advance - 10.0,
		starty + 5.0 + cairoui->extents.ascent);
	cairo_set_source_rgb(cairoui->cr, 1.0, 1.0, 1.0);
	cairo_show_text(cairoui->cr, error);
	return CAIROUI_CHANGED;
}

/*
 * keys always allowed for a field
 */
int cairoui_keyfield(int c) {
	return c == KEY_INIT ||
		c == KEY_REDRAW || c == KEY_REFRESH || c == KEY_RESIZE ||
		c == KEY_BACKSPACE || c == KEY_DC ||
		c == KEY_LEFT || c == KEY_RIGHT ||
		c == KEY_ENTER || c == '\n' ||
		c == '\033' || c == KEY_EXIT;
}

/*
 * allowed input for a numeric field
 */
int cairoui_keynumeric(int c) {
	return (c >= '0' && c <= '9') || cairoui_keyfield(c);
}

/*
 * generic field for a number
 */
int cairoui_number(int c, struct cairoui *cairoui,
		char *prompt, char *current, int *pos, char *error,
		int *destination, double min, double max) {
	double n;
	int res;

	switch (c) {
	case 'q':
		c = KEY_EXIT;
		break;

	case KEY_INIT:
		sprintf(current, "%d", *destination);
		break;

	case KEY_DOWN:
	case KEY_UP:
		n = current[0] == '\0' ? *destination : atof(current);
		n = n + (c == KEY_DOWN ? +1 : -1);
		if (n < min) {
			if (c == KEY_DOWN)
				n = min;
			else
				return CAIROUI_UNCHANGED;
		}
		if (n > max) {
			if (c == KEY_UP)
				n = max;
			else
				return CAIROUI_UNCHANGED;
		}
		sprintf(current, "%lg", n);
		c = KEY_REFRESH;
		break;

	default:
		if (cairoui_keynumeric(c))
			break;
		if (c == '-' && *pos == 0 && min < 0)
			break;
		return CAIROUI_UNCHANGED;
	}

	res = cairoui_field(c, cairoui, prompt, current, pos, error);
	if (res != CAIROUI_DONE)
		return res;

	if (current[0] == '\0')
		return CAIROUI_LEAVE;
	n = atof(current);
	if (n < min || n > max)
		return CAIROUI_INVALID;
	*destination = n;
	return CAIROUI_DONE;
}

/*
 * a label at the given number of labels from the bottom
 */
void cairoui_label(struct cairoui *cairoui, char *string, int bottom) {
	double width, x, y, h;

	cairo_identity_matrix(cairoui->cr);

	width = strlen(string) * cairoui->extents.max_x_advance;
	h = cairoui->extents.height;
	x = cairoui->dest.x + cairoui->dest.width / 2 - width / 2;
	y = cairoui->dest.y + cairoui->dest.height - bottom * (h + 20.0 + 2.0);

	cairo_set_source_rgb(cairoui->cr, 0, 0, 0);
	cairo_rectangle(cairoui->cr,
		x - 10.0, y - 20.0, width + 20.0, h + 20.0);
	cairo_fill(cairoui->cr);

	cairo_set_source_rgb(cairoui->cr, 0.8, 0.8, 0.8);
	cairo_move_to(cairoui->cr, x, y - 10.0 + cairoui->extents.ascent);
	cairo_show_text(cairoui->cr, string);

	cairo_stroke(cairoui->cr);
}

/*
 * resize output
 */
void cairoui_resize(struct cairoui *cairoui) {
	double x, y, width, height;

	cairo_identity_matrix(cairoui->cr);
	cairo_reset_clip(cairoui->cr);
	if (! cairoui->usearea ||
	    (cairoui->area.width == -1 && cairoui->area.height == -1)) {
		x = 0;
		y = 0;
		width = cairoui->cairodevice->width(cairoui->cairodevice);
		height = cairoui->cairodevice->height(cairoui->cairodevice);
	}
	else {
		x = cairoui->area.x;
		y = cairoui->area.y;
		width = cairoui->area.width;
		height = cairoui->area.height;
		cairo_rectangle(cairoui->cr,
			cairoui->area.x, cairoui->area.y,
			cairoui->area.width, cairoui->area.height);
		cairo_clip(cairoui->cr);
	}

	cairoui->dest.x = x + cairoui->margin;
	cairoui->dest.y = y + cairoui->margin;
	cairoui->dest.width = width - 2 * cairoui->margin;
	cairoui->dest.height = height - 2 * cairoui->margin;

	/* set font again because a resize may have implied the destruction and
	 * recreation of the context */
	cairo_select_font_face(cairoui->cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cairoui->cr, cairoui->fontsize);
	cairo_font_extents(cairoui->cr, &cairoui->extents);

	cairoui->resize(cairoui);
}

/*
 * reset output
 */
void cairoui_reset(struct cairoui *cairoui) {
	cairoui->cairodevice->blank(cairoui->cairodevice);
	cairoui_resize(cairoui);
}

/*
 * select window
 */
int cairoui_selectwindow(struct cairoui *cairoui, int window, int c) {
	int w;
	for (w = 0; cairoui->windowlist[w].name != NULL; w++)
		if (window == cairoui->windowlist[w].window &&
		    cairoui->windowlist[w].function != NULL)
			return cairoui->windowlist[w].function(c, cairoui);
	return cairoui->windowlist[0].window;
}

/*
 * draw the labels
 */
void cairoui_labels(struct cairoui *cairoui) {
	int l;
	for (l = 0; cairoui->labellist[l] != NULL; l++)
		cairoui->labellist[l](cairoui);
}

/*
 * initialize the labels
 */
void cairoui_initlabels(struct cairoui *cairoui) {
	cairo_surface_t *temp;
	cairo_t *prev;
	prev = cairoui->cr;
	temp = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
	cairoui->cr = cairo_create(temp);
	cairoui_labels(cairoui);
	cairo_destroy(cairoui->cr);
	cairo_surface_destroy(temp);
	cairoui->cr = prev;
}

/*
 * formatted print to a label; timeout=NO_TIMEOUT means infinite
 */
int cairoui_printlabel(struct cairoui *cairoui, char *help, int timeout,
		char *format, ...) {
	va_list ap;
	int res;

	va_start(ap, format);
	res = vsnprintf(help, 80, format, ap);
	va_end(ap);

	cairoui->timeout = timeout;
	cairoui->flush = TRUE;

	return res;
}

/*
 * empty callbacks
 */
void cairoui_nop(struct cairoui *cairoui) {
	(void) cairoui;
}
int cairoui_nopexternal(struct cairoui *cairoui, int window) {
	(void) cairoui;
	(void) window;
	return 0;
}

/*
 * cairoui structure defaults
 */
struct windowlist emptywindowlist[] = {{0, NULL, NULL}};
void (*emptylabellist[])(struct cairoui *) = {NULL};
void cairoui_default(struct cairoui *cairoui) {
	cairoui->usearea = 0;
	cairoui->area.width = -1;
	cairoui->area.height = -1;

	cairoui->draw = cairoui_nop;
	cairoui->resize = cairoui_nop;
	cairoui->update = cairoui_nop;
	cairoui->external = cairoui_nopexternal;
	cairoui->windowlist = emptywindowlist;
	cairoui->labellist = emptylabellist;

	cairoui->outname = "cairoui-out.txt";
	cairoui->outfile = NULL;
	cairoui->log = 0;
	cairoui->margin = 10.0;
	cairoui->fontsize = -1;

	cairoui->command.fd = -1;
	cairoui->command.stream = NULL;
}

/*
 * ensure the output file is open
 */
int ensureoutputfile(struct cairoui *cairoui) {
	if (cairoui->outfile != NULL)
		return 0;
	cairoui->outfile = fopen(cairoui->outname, "w");
	return cairoui->outfile == NULL;
}

/*
 * return values of windows, other than the next window
 */
struct cairoui_names
{	int win;		char *name;	}
cairoui_names[] = {
{	CAIROUI_DONE,		"DONE"		},
{	CAIROUI_LEAVE,		"LEAVE"		},
{	CAIROUI_INVALID,	"INVALID"	},
{	CAIROUI_UNCHANGED,	"UNCHANGED"	},
{	CAIROUI_CHANGED,	"CHANGED"	},
{	CAIROUI_REFRESH,	"REFRESH"	},
{	CAIROUI_EXIT,		"EXIT"		},
{	0,			NULL		}
};

/*
 * logging function
 */
void cairoui_logstatus(int level, char *prefix, int window,
		struct cairoui *cairoui, int c) {
	char *levname, levnum[8];
	char *keyname, keynum[8];
	char *winname, winnum[3];
	int w;

	if ((level & cairoui->log) == 0)
		return;

	switch (level) {
	case LEVEL_MAIN:
		levname = "MAIN";
		break;
	case LEVEL_DRAW:
		printf("draw\r\n");
		sleep(1);
		return;
	default:
		snprintf(levname = levnum, 8, "LEVEL%d", level);
	}

	switch (c) {
	case KEY_NONE:
		keyname = "KEY_NONE";
		break;
	case KEY_INIT:
		keyname = "KEY_INIT";
		break;
	case KEY_FINISH:
		keyname = "KEY_FINISH";
		break;
	case KEY_REFRESH:
		keyname = "KEY_REFRESH";
		break;
	case KEY_REDRAW:
		keyname = "KEY_REDRAW";
		break;
	case KEY_RESIZE:
		keyname = "KEY_RESIZE";
		break;
	case KEY_TIMEOUT:
		keyname = "KEY_TIMEOUT";
		break;
	case KEY_SUSPEND:
		keyname = "KEY_SUSPEND";
		break;
	case KEY_SIGNAL:
		keyname = "KEY_SIGNAL";
		break;
	case KEY_EXTERNAL:
		keyname = "KEY_EXTERNAL";
		break;
	default:
		if (isprint(c))
			snprintf(keyname = keynum, 8, "%c", c);
		else
			snprintf(keyname = keynum, 8, "[%d]", c);
	}

	winname = NULL;
	for (w = 0; cairoui->windowlist[w].name != NULL; w++)
		if (cairoui->windowlist[w].window == window) {
			winname = cairoui->windowlist[w].name;
			break;
		}
	for (w = 0; cairoui_names[w].name != NULL; w++)
		if (cairoui_names[w].win == window) {
			winname = cairoui_names[w].name;
			break;
		}
	if (winname == NULL)
		snprintf(winname = winnum, 3, "%d", window);

	ensureoutputfile(cairoui);
	fprintf(cairoui->outfile, "%-5s", levname);
	fprintf(cairoui->outfile, " %-12s", prefix);
	fprintf(cairoui->outfile, " %-15s", winname);
	fprintf(cairoui->outfile, " %-12s", keyname);
	fprintf(cairoui->outfile, " timeout=%-5d", cairoui->timeout);
	fprintf(cairoui->outfile, " redraw=%d", cairoui->redraw);
	fprintf(cairoui->outfile, " flush=%d\n", cairoui->flush);
	fflush(cairoui->outfile);
}

/*
 * reload handler
 */
void handler(int s) {
	if (s != SIGHUP)
		return;
	sig_reload = TRUE;
}

/*
 * main loop
 */
void cairoui_main(struct cairoui *cairoui, int firstwindow) {
	struct cairodevice *cairodevice = cairoui->cairodevice;
	struct command *command = &cairoui->command;
	int window, next, doc = cairoui->windowlist[0].window;
	int c;
	int pending;

	cairoui->cr = cairoui->cairodevice->context(cairoui->cairodevice);

	command->max = 4096;
	command->command = malloc(command->max);
	cairoui->paste = command->command;
	cairoui->outfile = NULL;
	if (cairoui->fontsize == -1)
		cairoui->fontsize = cairodevice->screenheight(cairodevice) / 25;

	cairoui_resize(cairoui);

	if (firstwindow != doc)
		cairoui_initlabels(cairoui);

	window = firstwindow;
	cairoui->reload = FALSE;
	cairoui->redraw = TRUE;
	cairoui->flush = TRUE;
	cairoui->timeout = NO_TIMEOUT;
	c = firstwindow == doc ? KEY_NONE : KEY_INIT;

	sig_reload = FALSE;
	signal(SIGHUP, handler);

	while (window != CAIROUI_EXIT) {

					/* draw document and labels */

		cairoui_logstatus(LEVEL_MAIN, "start", window, cairoui, c);
		if (cairoui->reload || sig_reload) {
			cairoui_logstatus(LEVEL_MAIN, "reload",
				window, cairoui, c);
			if (sig_reload)
				cairoui->redraw = TRUE;
			sig_reload = 0;
			cairoui->reload = FALSE;
			cairoui->update(cairoui);
			c = cairoui->redraw ? KEY_REDRAW : KEY_NONE;
		}
		if (! cairodevice->isactive(cairodevice))
			c = KEY_NONE;
		else if (c != KEY_INIT || cairoui->redraw) {
			cairoui_logstatus(LEVEL_MAIN, "draw",
				window, cairoui, c);
			if (cairoui->redraw && c != KEY_REDRAW) {
				cairodevice->clear(cairodevice);
				cairoui->redraw = FALSE;
				cairoui->draw(cairoui);
			}
			cairoui_labels(cairoui);
			if (cairoui->flush) {
				cairodevice->flush(cairodevice);
				cairoui->flush = FALSE;
			}
			if (cairoui->reload)
				continue;
		}

					/* read input */

		cairoui_logstatus(LEVEL_MAIN, "preinput", window, cairoui, c);
		if (c != KEY_NONE)
			pending = 0;
		else {
			pending = cairoui->timeout != NO_TIMEOUT &&
				cairoui->timeout != 0;
			c = cairodevice->input(cairodevice,
				cairoui->timeout, command);
			if (c != KEY_REDRAW)
				cairoui->timeout = NO_TIMEOUT;
			cairoui_logstatus(LEVEL_MAIN, "postinput",
				window, cairoui, c);
		}
		if (c == KEY_SUSPEND || c == KEY_SIGNAL ||
		    c == KEY_NONE || c == KEY_F(3) || c == KEY_F(4)) {
			c = KEY_NONE;
			continue;
		}
		if (c == KEY_REDRAW &&
		    cairodevice->doublebuffering(cairodevice) &&
		    ! cairoui->redraw) {
			cairoui->flush = TRUE;
			c = KEY_NONE;
			continue;
		}
		if (c == KEY_RESIZE || c == KEY_REDRAW || pending) {
			if (c == KEY_RESIZE)
				cairoui_resize(cairoui);
			cairoui->redraw = TRUE;
			cairoui->flush = FALSE;
			if (pending && c == KEY_TIMEOUT) {
				cairoui->timeout = NO_TIMEOUT;
				c = KEY_REFRESH;
				continue;
			}
			if (c == KEY_RESIZE || c == KEY_REDRAW) {
				c = KEY_REFRESH;
				continue;
			}
		}

					/* pass input to window or external */

		cairoui_logstatus(LEVEL_MAIN, "prewindow", window, cairoui, c);
		next = c == KEY_EXTERNAL ?
			cairoui->external(cairoui, window) :
			cairoui_selectwindow(cairoui, window, c);
		cairoui_logstatus(LEVEL_MAIN, "postwindow", next, cairoui, c);
		c = KEY_NONE;
		if (next == window)
			continue;
		if (next == CAIROUI_REFRESH) {
			/* for WINDOW_DOCUMENT, redraw the page and flush; all
			 * other windows: redraw the page and call the window
			 * again with KEY_REFRESH; flush then (not now) */
			cairoui->redraw = TRUE;
			cairoui->flush = window == doc;
			c = window == doc ? KEY_NONE : KEY_REFRESH;
			continue;
		}
		cairoui_selectwindow(cairoui, window, KEY_FINISH);
		if (next == doc) {
			cairoui->redraw = TRUE;
			cairoui->flush = TRUE;
			window = next;
			continue;
		}
		if (window != doc)
			cairoui->redraw = TRUE;
		window = next;
		c = KEY_INIT;
	}

	cairodevice->finish(cairodevice);
	if (command->fd != -1)
		fclose(command->stream);
	if (cairoui->outfile != NULL)
		fclose(cairoui->outfile);
}


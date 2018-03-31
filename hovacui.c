/*
 * hovacui.c
 *
 * view a pdf document, autozooming to the text
 *
 * todo:
 * - line of next scroll: where the top/bottom of the screen will be after
 *   scrolling up or down
 * - config opt "nolabel" for no label at all: skip the label part from draw()
 * - multiple files, list()-based window; return WINDOW_NEXT+n to tell main()
 *   which file to switch to; and/or have a field in struct output for the new
 *   file index or name
 * - non-expert mode: every unassigned key calls WINDOW_MENU
 * - in list(): separator, skip it when using a selected line
 * - add an optional help to the bottom of list()
 * - bookmarks, with field() for creating and list() for going to
 * - history of searches
 * - info(), based on list(): filename, number of pages, page format, etc.
 * - rotate
 * - numbermode: 2 is a, 22 is b, 222 is c, etc.
 * - remote control (via socket)
 * - split pdf viewing functions to pdfview.c and gui stuff to cairogui.c
 * - improve column-sorting rectangles (to be done in pdfrects.c)
 * - cache the textarea list of pages already scanned
 * - save last position(s) to $(HOME)/.pdfpositions
 * - include images (in pdfrects.c)
 * - utf8 in field()
 * - key to reset viewmode and fit direction to initial values
 * - introduce WINDOW_REFRESH, which causes the document to be redrawn with the
 *   labels on top and the same window to be called again with c=KEY_REDRAW; to
 *   avoid flickering, this requires flushing to be done only after the second
 *   call to the window, before accepting real input
 * - search(): utf8
 * - history of positions
 * - set output->redraw to FALSE when not moving
 * - order of rectangles for right-to-left and top-to-bottom scripts
 *   (generalize sorting function in pdfrects.c)
 * - i18n
 * - function to be possibly called before list() to wrap lines too long
 * - annotations and links:
 *   some key switches to anchor navigation mode, where keyup/keydown move to
 *   the next anchor (annotation or link) in displayed part of the current
 *   textbox (if any, otherwise they scroll as usual); this requires storing
 *   the current anchor both for its attribute (the text or the target) and for
 *   moving to the next; it was not needed for search, where the next match is
 *   just the first outside the area of the current textbox that is currently
 *   displayed
 * - x11
 * - avoid repeated operations (finding textarea, drawing, flushing)
 */

/*
 * how the document is mapped onto the screen
 * ------------------------------------------
 *
 * position->textarea->rect[position->box]
 *	the current textbox: the text-enclosing rectangle that is "focused"
 *	(drawn in light blue)
 *
 * position->viewbox
 *	the above rectangle, possibly enlarged to ensure the minimal width
 *
 * output->dest
 *	the area of the screen to use (all of it but a thin margin)
 *
 * the cairo transformation matrix is initially set so that position->viewbox
 * (rectangle in the document) is mapped onto output->dest (rectangle in the
 * screen), fitted by width and aligned to the top
 *
 * position->scrolly
 *	the shift applied to the document;
 *	used to move the viewbox up and down
 *
 * shifting by the scroll is done last; since transformations work as if
 * applied to the source in reverse order, this is like first scrolling the
 * document and then mapping position->viewbox onto the top of output->dest
 *
 * both position->scrollx and position->scrolly are adjusted to avoid parts
 * outside of the bounding box being displayed, wasting screen space
 *
 * all of this is for horizontal fitting mode: vertical fitting mode fits the
 * viewbox by height, but is otherwise the same; in both modes, the scroll is
 * relative to the origin of the current textbox
 */

/*
 * search
 * ------
 *
 * output->search
 *	the last searched string;
 *	needed to highlight the matches when switching pages
 *
 * output->forward
 *	whether to move forward or backward between search matches
 *
 * output->found
 *	the list of rectangles of the search matches in the current page
 *
 * the first match is located by scanning the document from the current textbox
 * to the last of the page, then in the following pages until coming back to
 * the original page
 *
 * matches that are in the current textbox but fall before the part of it that
 * is currently displayed on the screen are ignored
 *
 * the next match is the same but also excludes matches that are inside the
 * displayed part of the current textbox
 */

/*
 * user interface
 * --------------
 *
 * a simple user interface is implemented: windows receive input, labels do
 * not; both are functions with state stored as static variables or fields in
 * the output structure
 *
 * int window(int c, struct position *position, struct output *output);
 *	the given window is activated (if not already) and receive input c:
 *	- input is a key, but can also be KEY_INIT or KEY_REDRAW
 *	- output is the next window to become active
 *
 * void label(struct position *position, struct output *output);
 *	all label functions are called at each step; they have to decide by
 *	themselves whether to draw something; they choose based on the content
 *	of the position and output structures; for example, the pagenumber()
 *	labels draws when the page number changes and when output->pagenumber
 *	is true
 *
 * both windows and label draw themselves
 *
 * each window is a specific instance of a generic window:
 *
 * field()
 *	a generic textfield
 *	called by search()
 *
 * number()
 *	a textfield for a number
 *	called by gotopage(), minwidth() and textdistance()
 *
 * list()
 *	a list of strings, possibly with a selected one
 *	called by help(), tutorial() and menu()
 *
 * a particular window is document(), which draws nothing and deal with normal
 * input (when no other window is active)
 *
 * for each input (or at input timeout, if output->timeout is true), the active
 * window is called, then the document is (re)drawn if output->redraw is true,
 * then the label functions are all called (each decide by itself whether to
 * draw something)
 *
 * this sequence allows for a window to draw itself over the document without
 * the need to redraw the document first; it however disallows for example to
 * move the document or to show/remove a label while the window is active;
 * therefore, everthing the window needs to communicate has to be drawn within
 * its own screen space
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <poppler.h>
#include <ncurses.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include "cairofb.h"
#include "pdfrects.h"
#include "vt.h"

/*
 * the windows
 */
enum window {
	WINDOW_DOCUMENT,
	WINDOW_HELP,
	WINDOW_TUTORIAL,
	WINDOW_GOTOPAGE,
	WINDOW_SEARCH,
	WINDOW_VIEWMODE,
	WINDOW_FITDIRECTION,
	WINDOW_MENU,
	WINDOW_WIDTH,
	WINDOW_DISTANCE,
	WINDOW_EXIT
};

/*
 * imaginary keys
 */
#define KEY_INIT	((KEY_MAX) + 1)
#define KEY_REDRAW	((KEY_MAX) + 2)
#define KEY_TIMEOUT	((KEY_MAX) + 3)
#define KEY_SIGNAL	((KEY_MAX) + 4)

/*
 * output parameters
 */
struct output {
	/* the cairo surface */
	cairo_t *cr;

	/* the destination rectangle */
	PopplerRectangle dest;

	/* the pixel aspect */
	double aspect;

	/* the minimal textbox-to-textbox distance */
	double distance;

	/* the minimal width; tells the maximal zoom */
	double minwidth;

	/* zoom to: 0=text, 1=boundingbox, 2=page */
	int viewmode;

	/* fit horizontally (1), vertically (2) or both (0) */
	int fit;

	/* scroll distance, as a fraction of the screen size */
	double scroll;

	/* whether the document has to be redrawn */
	int redraw;

	/* whether the output is to be flushed */
	int flush;

	/* stop input on timeout and return KEY_TIMEOUT */
	int timeout;

	/* labels */
	gboolean pagenumber;
	gboolean totalpages;
	gboolean showmode;
	gboolean showfit;
	gboolean filename;
	char help[80];

	/* size of font */
	cairo_font_extents_t extents;

	/* search */
	char search[100];
	gboolean forward;
	GList *found;
};

/*
 * a position within a document
 */
struct position {
	/* the poppler document */
	char *filename;
	PopplerDocument *doc;

	/* the current page, its bounding box, the total number of pages */
	int npage, totpages;
	PopplerPage *page;
	PopplerRectangle *boundingbox;

	/* the text rectangle currently viewed, all of them in the page */
	RectangleList *textarea;
	int box;
	PopplerRectangle *viewbox;

	/* the scroll within the rectangle currently viewed */
	double scrollx;
	double scrolly;
};

/*
 * initialize position
 */
void initposition(struct position *position) {
	position->npage = 0;
	position->page = NULL;
	position->boundingbox = NULL;
	position->textarea = NULL;
	position->box = 0;
	position->viewbox = NULL;
	position->scrollx = 0;
	position->scrolly = 0;
}

/*
 * a rectangle as large as the whole page
 */
PopplerRectangle *pagerectangle(PopplerPage *page) {
	PopplerRectangle *r;
	r = poppler_rectangle_new();
	r->x1 = 0;
	r->y1 = 0;
	poppler_page_get_size(page, &r->x2, &r->y2);
	return r;
}

/*
 * free a glist of rectangles
 */
void freeglistrectangles(GList *list) {
	GList *l;
	for (l = list; l != NULL; l = l->next)
		poppler_rectangle_free((PopplerRectangle *) l->data);
	g_list_free(list);
}

/*
 * read the current page without its textarea
 */
int readpageraw(struct position *position, struct output *output) {
	position->page =
		poppler_document_get_page(position->doc, position->npage);

	freeglistrectangles(output->found);
	output->found = output->search[0] == '\0' ?
		NULL : poppler_page_find_text(position->page, output->search);

	return 0;
}

/*
 * determine the textarea of the current page
 */
int textarea(struct position *position, struct output *output) {
	rectanglelist_free(position->textarea);
	poppler_rectangle_free(position->boundingbox);

	switch (output->viewmode) {
	case 0:
		position->textarea =
			rectanglelist_textarea_distance(position->page,
				output->distance);
		if (position->textarea->num == 0) {
			rectanglelist_free(position->textarea);
			position->textarea = NULL;
			position->boundingbox = NULL;
			break;
		}
		rectanglelist_sort(position->textarea);
		position->boundingbox =
			rectanglelist_joinall(position->textarea);
		break;
	case 1:
		position->boundingbox =
			rectanglelist_boundingbox(position->page);
		position->textarea = NULL;
		break;
	case 2:
		position->boundingbox = pagerectangle(position->page);
		position->textarea = NULL;
		break;
	}
	if (position->boundingbox == NULL)
		position->boundingbox = pagerectangle(position->page);
	if (position->textarea == NULL) {
		position->textarea = rectanglelist_new(1);
		rectanglelist_add(position->textarea, position->boundingbox);
	}

	return 0;
}

/*
 * read the current page
 */
int readpage(struct position *position, struct output *output) {
	readpageraw(position, output);
	textarea(position, output);
	return 0;
}

/*
 * translate from textbox coordinates to screen coordinates and back
 */
double xdoctoscreen(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_user_to_device(output->cr, &xx, &yy);
	return xx;
}
double xscreentodoc(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_device_to_user(output->cr, &xx, &yy);
	return xx;
}
double ydoctoscreen(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_user_to_device(output->cr, &xx, &yy);
	return yy;
}
double yscreentodoc(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_device_to_user(output->cr, &xx, &yy);
	return yy;
}

/*
 * translate distances from screen coordinates to textbox coordinates
 */
double xscreentodocdistance(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_device_to_user_distance(output->cr, &xx, &yy);
	return xx;
}
double yscreentodocdistance(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_device_to_user_distance(output->cr, &xx, &yy);
	return yy;
}

/*
 * size of destination rectangle translated to document coordinates
 */
double xdestsizetodoc(struct output *output) {
	return xscreentodocdistance(output, output->dest.x2 - output->dest.x1);
}
double ydestsizetodoc(struct output *output) {
	return yscreentodocdistance(output, output->dest.y2 - output->dest.y1);
}

/*
 * change scrollx and scrolly to avoid space outside boundingbox being shown
 */
void adjustscroll(struct position *position, struct output *output) {

	/* some space at the right of the bounding box is shown */
	if (xdoctoscreen(output, position->boundingbox->x2 - position->scrollx)
	    < output->dest.x2)
		position->scrollx = position->boundingbox->x2 -
			xscreentodoc(output, output->dest.x2);

	/* some space at the left of the bounding box is shown */
	if (xdoctoscreen(output, position->boundingbox->x1 - position->scrollx)
	    > output->dest.x1)
		position->scrollx = position->boundingbox->x1 -
			xscreentodoc(output, output->dest.x1);

	/* bounding box too narrow to fill the screen */
	if (position->boundingbox->x2 - position->boundingbox->x1 <
	    xscreentodocdistance(output, output->dest.x2 - output->dest.x1))
		position->scrollx =
			(position->boundingbox->x1 +
			 position->boundingbox->x2) / 2 -
			xscreentodoc(output,
				(output->dest.x1 + output->dest.x2) / 2);

	/* some space below the bounding box is shown */
	if (ydoctoscreen(output, position->boundingbox->y2 - position->scrolly)
	    < output->dest.y2)
		position->scrolly = position->boundingbox->y2 -
			yscreentodoc(output, output->dest.y2);

	/* some space over the bounding box is shown */
	if (ydoctoscreen(output, position->boundingbox->y1 - position->scrolly)
	    > output->dest.y1)
		position->scrolly = position->boundingbox->y1 -
			yscreentodoc(output, output->dest.y1);

	/* bounding box too short to fill the screen */
	if (position->boundingbox->y2 - position->boundingbox->y1 <
	    yscreentodocdistance(output, output->dest.y2 - output->dest.y1))
		position->scrolly =
			(position->boundingbox->y1 +
			 position->boundingbox->y2) / 2 -
			yscreentodoc(output,
				(output->dest.y1 + output->dest.y2) / 2);

	return;
}

/*
 * adjust width of viewbox to the minimum allowed
 */
void adjustviewbox(struct position *position, struct output *output) {
	double d;
	PopplerRectangle *viewbox;
	int fitmode;

	fitmode = output->fit == 0 ? 3 : output->fit;
	viewbox = position->viewbox;

	if ((fitmode & 1) && viewbox->x2 - viewbox->x1 < output->minwidth) {
		d = output->minwidth - viewbox->x2 + viewbox->x1;
		viewbox->x1 -= d / 2;
		viewbox->x2 += d / 2;
	}

	if ((fitmode & 2) && viewbox->y2 - viewbox->y1 < output->minwidth) {
		d = output->minwidth - viewbox->y2 + viewbox->y1;
		viewbox->y1 -= d / 2;
		viewbox->y2 += d / 2;
	}
}

/*
 * move to position
 */
void moveto(struct position *position, struct output *output) {
	PopplerRectangle scaled;

	cairo_identity_matrix(output->cr);

	/* scale to match the screen aspect; also scale the destination
	 * rectangle in the opposite way, so that position->viewbox is still
	 * mapped to output->dest in spite of the scaling */
	scaled = output->dest;
	if (output->fit & 0x1) {
		cairo_scale(output->cr, 1.0, output->aspect);
		scaled.y1 = scaled.y1 / output->aspect;
		scaled.y2 = scaled.y2 / output->aspect;
	}
	else {
		cairo_scale(output->cr, 1.0 / output->aspect, 1.0);
		scaled.x1 = scaled.x1 * output->aspect;
		scaled.x2 = scaled.x2 * output->aspect;
	}

	poppler_rectangle_free(position->viewbox);
	position->viewbox = poppler_rectangle_copy
		(&position->textarea->rect[position->box]);
	adjustviewbox(position, output);
	rectangle_map_to_cairo(output->cr, &scaled, position->viewbox,
		output->fit & 0x1, output->fit & 0x2, TRUE, TRUE, TRUE);

	adjustscroll(position, output);
	cairo_translate(output->cr, -position->scrollx, -position->scrolly);
}

/*
 * go to the top of current textbox
 */
int toptextbox(struct position *position, struct output *output) {
	(void) output;
	position->scrollx = 0;
	position->scrolly = 0;
	return 0;
}

/*
 * go to the top of the first textbox of the page
 */
int firsttextbox(struct position *position, struct output *output) {
	position->box = 0;
	return toptextbox(position, output);
}

/*
 * move to the start of the next page
 */
int nextpage(struct position *position, struct output *output) {
	if (position->npage + 1 >= position->totpages)
		return -1;

	position->npage++;
	readpage(position, output);
	return firsttextbox(position, output);
}

/*
 * move to the top of the next textbox
 */
int nexttextbox(struct position *position, struct output *output) {
	if (position->box + 1 >= position->textarea->num)
		return nextpage(position, output);

	position->box++;
	return toptextbox(position, output);
}

/*
 * scroll down
 */
int scrolldown(struct position *position, struct output *output) {
	moveto(position, output);
	/* bottom of textbox already within the destination rectangle */
	if (ydoctoscreen(output, position->textarea->rect[position->box].y2) <=
	    output->dest.y2 + 0.01)
		return nexttextbox(position, output);

	position->scrolly += yscreentodocdistance(output,
		(output->dest.y2 - output->dest.y1) * output->scroll);
	return 0;
}

/*
 * scroll right
 */
int scrollright(struct position *position, struct output *output) {
	moveto(position, output);
	/* right edge of textbox already within the destination rectangle */
	if (xdoctoscreen(output, position->textarea->rect[position->box].x2) <=
	    output->dest.x2 + 0.01)
		return nexttextbox(position, output);

	position->scrollx += xscreentodocdistance(output,
		(output->dest.x2 - output->dest.x1) * output->scroll);
	return 0;
}

/*
 * move to the bottom/right of the current textbox
 */
int bottomtextbox(struct position *position, struct output *output) {
	position->scrollx = 0;
	position->scrolly = 0;
	moveto(position, output);
	position->scrollx =
		position->viewbox->x2 - xscreentodoc(output, output->dest.x2);
	position->scrolly =
		position->viewbox->y2 - yscreentodoc(output, output->dest.y2);
	return 0;
}

/*
 * go to the bottom of the last textbox in the page
 */
int lasttextbox(struct position *position, struct output *output) {
	position->box = position->textarea->num - 1;
	return bottomtextbox(position, output);
}

/*
 * move to the bottom of the previous page
 */
int prevpage(struct position *position, struct output *output) {
	if (position->npage <= 0)
		return -1;

	position->npage--;
	readpage(position, output);
	return lasttextbox(position, output);
}

/*
 * move to the bottom of previous textbox
 */
int prevtextbox(struct position *position, struct output *output) {
	if (position->box - 1 < 0)
		return prevpage(position, output);

	position->box--;
	return bottomtextbox(position, output);
}

/*
 * scroll up
 */
int scrollup(struct position *position, struct output *output) {
	moveto(position, output);
	/* top of current textbox already within the destination rectangle */
	if (ydoctoscreen(output, position->textarea->rect[position->box].y1) >=
	    output->dest.y1 - 0.01)
		return prevtextbox(position, output);

	position->scrolly -= yscreentodocdistance(output,
		(output->dest.y2 - output->dest.y1) * output->scroll);
	return 0;
}

/*
 * scroll left
 */
int scrollleft(struct position *position, struct output *output) {
	moveto(position, output);
	/* left edge of the textbox already within the destination rectangle */
	if (xdoctoscreen(output, position->textarea->rect[position->box].x1) >=
	    output->dest.x1 - 0.01)
		return prevtextbox(position, output);

	position->scrollx -= xscreentodocdistance(output,
		(output->dest.x2 - output->dest.x1) * output->scroll);
	return 0;
}

/*
 * check whether a rectangle is before/after the top/bottom of the screen
 */
int relativescreen(struct output *output, PopplerRectangle *r,
		gboolean inscreen, gboolean after) {
	double x, y;

	if (after) {
		x = xdoctoscreen(output, r->x1);
		y = ydoctoscreen(output, r->y1);
		if (inscreen)
			return x >= output->dest.x1 && y >= output->dest.y1;
		else
			return x > output->dest.x2 || y > output->dest.y2;
	}
	else {
		x = xdoctoscreen(output, r->x2);
		y = ydoctoscreen(output, r->y2);
		if (inscreen)
			return x <= output->dest.x2 && y <= output->dest.y2;
		else
			return x < output->dest.x1 || y < output->dest.y1;
	}
}

/*
 * position a rectangle in the current textbox at the top or bottom of screen
 */
void scrolltorectangle(struct position *position, struct output *output,
		PopplerRectangle *r, gboolean top) {
	PopplerRectangle *t;

	t = &position->textarea->rect[position->box];
	toptextbox(position, output);
	moveto(position, output);
	if (output->fit & 0x2)
		position->scrollx = top ?
			r->x1 - t->x1 - 40 :
			r->x2 - t->x1 + 40 - xdestsizetodoc(output);
	if (output->fit & 0x1)
		position->scrolly = top ?
			r->y1 - t->y1 - 40 :
			r->y2 - t->y1 + 40 - ydestsizetodoc(output);
	adjustscroll(position, output);
}

/*
 * go to the next match in the page, if any
 */
int nextpagematch(struct position *position, struct output *output,
		gboolean inscreen, gboolean first) {
	gboolean forward;
	int b;
	int end, step;
	double width, height, prev;
	PopplerRectangle *t, r;
	GList *o, *l;

	if (output->found == NULL)
		return -1;

	forward = output->forward;
	end = forward ? position->textarea->num : -1;
	step = forward ? +1 : -1;

	if (forward)
		o = output->found;
	else {
		o = g_list_copy(output->found);
		o = g_list_reverse(o);
	}

	poppler_page_get_size(position->page, &width, &height);
	for (b = position->box; b != end; b += step) {
		t = &position->textarea->rect[b];
		for (l = o; l != NULL; l = l->next) {
			r = * (PopplerRectangle *) l->data;
			prev = r.y1;
			r.y1 = height - r.y2;
			r.y2 = height - prev;

			if (! rectangle_contain(t, &r))
				continue;
			if (first &&
			    ! relativescreen(output, &r, inscreen, forward))
				continue;

			position->box = b;
			scrolltorectangle(position, output, &r, forward);

			if (! forward)
				g_list_free(o);
			return 0;
		}
		inscreen = TRUE;
		first = FALSE;
	}

	if (! forward)
		g_list_free(o);
	return -1;
}

/*
 * go to the next match, from/after the displayed area of the current textbox
 */
int gotomatch(struct position *position, struct output *output,
		gboolean inscreen) {
	gboolean first;
	int n;
	struct position scan;

	freeglistrectangles(output->found);
	output->found = NULL;
	if (output->search[0] == '\0')
		return -2;

	moveto(position, output);

	first = TRUE;
	scan = *position;
	output->found = poppler_page_find_text(scan.page, output->search);
	for (n = 0; n < scan.totpages + 1; n++) {
		if (! nextpagematch(&scan, output, inscreen, first)) {
			*position = scan;
			return 0;
		}
		inscreen = TRUE;
		first = FALSE;
		scan.npage = (scan.npage + (output->forward ? 1 : -1)) %
				scan.totpages;
		if (scan.npage == -1)
			scan.npage = scan.totpages - 1;
		readpageraw(&scan, output);
		if (output->found == NULL)
			continue;
		scan.textarea = NULL; // otherwise position->textarea is freed
		textarea(&scan, output);
		scan.box = output->forward ? 0 : (scan.textarea->num - 1);
	}

	return -1;
}

/*
 * go to the first or next match, if any
 */
int firstmatch(struct position *position, struct output *output) {
	return gotomatch(position, output, TRUE);
}
int nextmatch(struct position *position, struct output *output) {
	return gotomatch(position, output, FALSE);
}

/*
 * check whether the bounding box is all in the screen
 */
int boundingboxinscreen(struct position *position, struct output *output) {
	if (position->boundingbox->x2 - position->boundingbox->x1 >
	    xdestsizetodoc(output))
		return FALSE;
	if (position->boundingbox->y2 - position->boundingbox->y1 >
	    ydestsizetodoc(output))
		return FALSE;
	return TRUE;
}

/*
 * document window
 */
int document(int c, struct position *position, struct output *output) {
	output->redraw = TRUE;

	switch (c) {
	case KEY_INIT:
	case KEY_REDRAW:
	case 'r':
		readpage(position, output);
		break;
	case 'q':
		return WINDOW_EXIT;
	case KEY_HELP:
	case 'h':
		output->redraw = FALSE;
		return WINDOW_HELP;
	case KEY_OPTIONS:
	case 'm':
		output->redraw = FALSE;
		return WINDOW_MENU;
	case KEY_MOVE:
	case 'g':
		output->redraw = FALSE;
		return WINDOW_GOTOPAGE;
	case 'w':
		output->redraw = FALSE;
		return WINDOW_WIDTH;
	case 't':
		output->redraw = FALSE;
		return WINDOW_DISTANCE;
	case KEY_FIND:
	case '/':
	case '?':
		output->forward = c != '?';
		output->redraw = FALSE;
		return WINDOW_SEARCH;
	case 'n':
	case 'p':
		output->forward = c == 'n';
		nextmatch(position, output);
		break;
	case ' ':
		if (output->fit & 0x1)
			scrolldown(position, output);
		else if (output->fit & 0x2)
			scrollright(position, output);
		else
			nexttextbox(position, output);
		break;
	case KEY_DOWN:
		scrolldown(position, output);
		break;
	case KEY_UP:
		scrollup(position, output);
		break;
	case KEY_LEFT:
		scrollleft(position, output);
		break;
	case KEY_RIGHT:
		scrollright(position, output);
		break;
	case KEY_HOME:
		firsttextbox(position, output);
		break;
	case KEY_END:
		lasttextbox(position, output);
		break;
	case KEY_NPAGE:
		nextpage(position, output);
		break;
	case KEY_PPAGE:
		prevpage(position, output);
		break;
	case 'v':
		output->viewmode = (output->viewmode + 1) % 3;
		firsttextbox(position, output);
		readpage(position, output);
		break;
	case 'z':
		if (output->minwidth <= 0)
			break;
		output->minwidth -= 10;
		if (output->fit & 0x1)
			position->scrollx = 0;
		else
			position->scrolly = 0;
		break;
	case 'Z':
		if (boundingboxinscreen(position, output))
			break;
		output->minwidth += 10;
		break;
	case 'f':
		output->fit = (output->fit + 1) % 3;
		position->scrollx = 0;
		position->scrolly = 0;
		break;
	case 's':
		output->timeout = 3000;
		output->pagenumber = TRUE;
		output->showmode = TRUE;
		output->showfit = TRUE;
		output->filename = TRUE;
		break;
	default:
		;
	}

	return WINDOW_DOCUMENT;
}

/*
 * print a line at current position
 */
void printline(cairo_t *cr, char *string, int advance) {
	double x, y;
	cairo_get_current_point(cr, &x, &y);
	cairo_show_text(cr, string);
	cairo_move_to(cr, x, y + advance);
}

/*
 * show a list of strings
 */
int list(int c, struct output *output, char *viewtext[],
		int *line, int *selected) {
	double percent = 0.8;
	double width = output->dest.x2 - output->dest.x1;
	double height = output->dest.y2 - output->dest.y1;
	double marginx = width * (1 - percent) / 2;
	double marginy = height * (1 - percent) / 2;
	double borderx = 10.0;
	double bordery = 10.0;
	double titleheight = output->extents.height + 2 * bordery;
	double textheight, listheight;
	double startx = output->dest.x1 + marginx;
	double starty = output->dest.y1 + marginy;
	double startlist = starty + titleheight + bordery;
	int n, l, lines;

	for (n = 1; viewtext[n] != NULL; n++) {
	}

	cairo_identity_matrix(output->cr);
	lines = (int) (height * percent - titleheight - bordery * 2) /
		(int) output->extents.height;
	textheight = (n - 1 < lines ? n - 1 : lines) * output->extents.height;
	listheight = textheight + 2 * bordery;
	height = textheight + listheight;

	switch (c) {
	case KEY_DOWN:
		if (selected != NULL && *selected >= n - 1)
			return 0;
		if (selected != NULL && *selected < *line + lines)
			(*selected)++;
		else if (*line >= n - lines - 1)
			return 0;
		else {
			if (selected != NULL)
				(*selected)++;
			(*line)++;
		}
		output->redraw = TRUE;
		break;
	case KEY_UP:
		if (selected != NULL && *selected <= 1)
			return 0;
		else if(selected != NULL && *selected > *line + 1)
			(*selected)--;
		else if (*line <= 0)
			return 0;
		else {
			if (selected != NULL)
				(*selected)--;
			(*line)--;
		}
		output->redraw = TRUE;
		break;
	case KEY_INIT:
	case KEY_REDRAW:
	case KEY_TIMEOUT:
		break;
	case KEY_ENTER:
	case '\n':
		if (selected != NULL) {
			return *selected;
			break;
		}
		/* fallthrough */
	default:
		output->redraw = TRUE;
		return -1;
	}

	cairo_set_source_rgb(output->cr, 0.6, 0.6, 0.8);
	cairo_rectangle(output->cr,
		startx, starty, width - marginx * 2, titleheight);
	cairo_fill(output->cr);
	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		startx + borderx, starty + bordery + output->extents.ascent);
	printline(output->cr, viewtext[0], output->extents.height);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + marginy + titleheight,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		listheight);
	cairo_fill(output->cr);
	cairo_stroke(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_save(output->cr);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx, startlist,
		width - marginx * 2, textheight);
	cairo_clip(output->cr);

	cairo_translate(output->cr, 0.0, - output->extents.height * *line);
	for (l = 1; viewtext[l] != NULL; l++) {
		if (selected == NULL || l != *selected)
			cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
		else {
			cairo_set_source_rgb(output->cr, 0.3, 0.3, 0.3);
			cairo_rectangle(output->cr,
				startx,
				startlist + output->extents.height * (l - 1),
				width - 2 * marginx,
				output->extents.height);
			cairo_fill(output->cr);
			cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
		}
		cairo_move_to(output->cr,
			startx + borderx,
			startlist + output->extents.height * (l - 1) +
				output->extents.ascent);
		cairo_show_text(output->cr, viewtext[l]);
	}
	cairo_stroke(output->cr);
	cairo_restore(output->cr);

	if (lines < n - 1) {
		cairo_rectangle(output->cr,
			output->dest.x2 - marginx - borderx,
			output->dest.y1 + marginy + titleheight +
				*line / (double) (n - 1) * listheight,
			borderx,
			lines / (double) (n - 1) * listheight);
		cairo_fill(output->cr);
		cairo_stroke(output->cr);
	}

	output->redraw = FALSE;
	output->flush = TRUE;
	return 0;
}

/*
 * help
 */
int help(int c, struct position *position, struct output *output) {
	static char *helptext[] = {
		"hovacui - pdf viewer with autozoom to text",
		"PageUp     previous page",
		"PageDown   next page",
		"Home       top of page",
		"End        bottom of page",
		"m          main menu",
		"v          change view mode:",
		"           textarea, boundingbox, page",
		"f          change fitting direction:",
		"           horizontal, vertical, both",
		"w z Z      minimal width: set, -, +",
		"           (determines the maximal zoom)",
		"t          text-to-text distance",
		"g          go to page",
		"/ ?        search forward or backward",
		"n p        next or previous search match",
		"s          show current mode and page",
		"h          help",
		"q          quit",
		"",
		"any key to continue",
		NULL
	};
	static int line = 0;
	(void) position;

	return list(c, output, helptext, &line, NULL) == 0 ?
		WINDOW_HELP : WINDOW_DOCUMENT;
}

/*
 * tutorial
 */
int tutorial(int c, struct position *position, struct output *output) {
	static char *tutorialtext[] = {
		"hovacui - pdf viewer with autozoom to text",
		"hovacui displays a block of text at time",
		"the current block is bordered in blue",
		"",
		"zoom is automatic",
		"move by cursor %s " "and PageUp/PageDown",
		"",
		"key h for help",
		"key m for menu",
		"key v for whole page view",
		"",
		"space bar to view document",
		NULL
	};
	static char cursor[100];
	static int line = 0;
	int i;
	(void) position;

	for (i = 0; tutorialtext[i] != NULL; i++)
		if (strstr(tutorialtext[i], "%s")) {
			sprintf(cursor, tutorialtext[i],
				output->fit == 0x1 ? "Up/Down" : "Left/Right");
			tutorialtext[i] = cursor;
		}

	return c == 'h' ? WINDOW_HELP :
		list(c, output, tutorialtext, &line, NULL) == 0 ?
			WINDOW_TUTORIAL : WINDOW_DOCUMENT;
}

/*
 * viewmode menu
 */
int viewmode(int c, struct position *position, struct output *output) {
	static char *viewmodetext[] = {
		"view mode",
		"text area",
		"boundingbox",
		"page",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	if (c == KEY_INIT)
		selected = output->viewmode + 1;

	res = list(c, output, viewmodetext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_VIEWMODE;
	case 1:
	case 2:
	case 3:
		output->viewmode = res - 1;
		firsttextbox(position, output);
		/* fallthrough */
	default:
		selected = 1;
		return WINDOW_DOCUMENT;
	}
}

/*
 * fit direction menu
 */
int fitdirection(int c, struct position *position, struct output *output) {
	static char *fitdirectiontext[] = {
		"fit direction",
		"horizontal",
		"vertical",
		"both",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	if (c == KEY_INIT)
		selected = output->fit == 0 ? 3 : output->fit;

	res = list(c, output, fitdirectiontext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_FITDIRECTION;
	case 1:
	case 2:
	case 3:
		output->fit = res == 3 ? 0 : res;
		firsttextbox(position, output);
		/* fallthrough */
	default:
		selected = 1;
		return WINDOW_DOCUMENT;
	}
}

/*
 * general menu
 */
int menu(int c, struct position *position, struct output *output) {
	static char *menutext[] = {
		"hovacui - menu",
		"(g) go to page",
		"(/) search",
		"(v) view mode",
		"(f) fit direction",
		"(w) minimal width",
		"(t) text distance",
		"(h) help",
		"(q) quit",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	switch (c) {
	case 'g':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_GOTOPAGE;
	case '/':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_SEARCH;
	case 'v':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_VIEWMODE;
	case 'f':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_FITDIRECTION;
	case 'w':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_WIDTH;
	case 't':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_DISTANCE;
	case 'h':
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_HELP;
	case 'q':
		return WINDOW_EXIT;
	}

	res = list(c, output, menutext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_MENU;
	case 1:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_GOTOPAGE;
	case 2:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_SEARCH;
	case 3:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_VIEWMODE;
	case 4:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_FITDIRECTION;
	case 5:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_WIDTH;
	case 6:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_DISTANCE;
	case 7:
		selected = 1;
		output->redraw = TRUE;
		return WINDOW_HELP;
	case 8:
		return WINDOW_EXIT;
	case 9:
		strcpy(output->help, "unimplemented");
		/* fallthrough */
	default:
		selected = 1;
		return WINDOW_DOCUMENT;
	}
}

/*
 * generic textfield
 */
void field(int c, struct output *output,
		char *prompt, char *current, char *error, char *help) {
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (output->dest.x2 - output->dest.x1) * prop;
	double marginy = 20.0;
	double startx = output->dest.x1 + marginx;
	double starty = output->dest.y1 + marginy;
	cairo_text_extents_t te;
	int l;

	l = strlen(current);
	if (c == KEY_BACKSPACE || c == KEY_DC) {
		if (l < 0)
			return;
		current[l - 1] = '\0';
	}
	else if (c != KEY_INIT && c != KEY_REDRAW) {
		if (l > 30)
			return;
		current[l] = c;
		current[l + 1] = '\0';
	}
	else if ((c == KEY_INIT || c == KEY_REDRAW) && help != NULL)
		strcpy(output->help, help);

	cairo_identity_matrix(output->cr);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		startx,
		starty,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		output->extents.height + 10);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		startx + 10.0,
		starty + 5.0 + output->extents.ascent);
	cairo_show_text(output->cr, prompt);
	cairo_show_text(output->cr, current);
	cairo_show_text(output->cr, "_ ");
	if (error == NULL)
		return;
	cairo_text_extents(output->cr, error, &te);
	cairo_set_source_rgb(output->cr, 0.8, 0.0, 0.0);
	cairo_rectangle(output->cr,
		output->dest.x2 - marginx - te.x_advance - 20.0,
		starty,
		te.x_advance + 20.0,
		output->extents.height + 10.0);
	cairo_fill(output->cr);
	cairo_move_to(output->cr,
		output->dest.x2 - marginx - te.x_advance - 10.0,
		starty + 5.0 + output->extents.ascent);
	cairo_set_source_rgb(output->cr, 1.0, 1.0, 1.0);
	cairo_show_text(output->cr, error);
}

/*
 * keys always allowed for a field
 */
int keyfield(int c) {
	return c == KEY_INIT || c == KEY_REDRAW ||
		c == KEY_BACKSPACE || c == KEY_DC;
}

/*
 * allowed input for a numeric field
 */
int keynumeric(int c) {
	return (c >= '0' && c <= '9') || keyfield(c);
}

/*
 * generic field for a number
 */
int number(int c, struct output *output,
		char *prompt, char *current, char *error, char *help,
		double *destination, double min, double max) {
	double n;

	switch (c) {
	case '\033':
	case KEY_EXIT:
	case 'q':
		return -1;

	case KEY_INIT:
		sprintf(current, "%lg", *destination);
		/* fallthrough */
	case KEY_REDRAW:
		field(c, output, prompt, current, error, help);
		return 0;

	case KEY_DOWN:
	case KEY_UP:
		n = atof(current);
		n = n + (c == KEY_DOWN ? +1 : -1);
		if (n < min) {
			if (c == KEY_DOWN)
				n = min;
			else
				return 0;
		}
		if (n > max) {
			if (c == KEY_UP)
				n = max;
			else
				return 0;
		}
		sprintf(current, "%lg", n);
		c = KEY_REDRAW;
		field(c, output, prompt, current, error, help);
		return 0;

	case KEY_ENTER:
	case '\n':
		if (current[0] == '\0')
			return -1;
		n = atof(current);
		if (n < min || n > max)
			return -2;
		*destination = n;
		return 1;

	default:
		if (keynumeric(c))
			field(c, output, prompt, current, error, help);
		return 0;
	}
}

/*
 * field for a search keyword
 */
int search(int c, struct position *position, struct output *output) {
	static char searchstring[100] = "";
	char *prompt = "find: ";
	char *error = NULL;

	if (c == '\033' || c == KEY_EXIT) {
		searchstring[0] = '\0';
		strcpy(output->search, searchstring);
		return WINDOW_DOCUMENT;
	}

	if (c == KEY_ENTER || c == '\n') {
		strcpy(output->search, searchstring);
		if (searchstring[0] == '\0')
			return WINDOW_DOCUMENT;
		if (firstmatch(position, output) != -1) {
			searchstring[0] = '\0';
			strcpy(output->help,
				"n=next matches p=previous matches");
			output->timeout = 2000;
			return WINDOW_DOCUMENT;
		}
		c = KEY_REDRAW;
		error = "no match";
	}

	field(c, output, prompt, searchstring, error, NULL);
	return WINDOW_SEARCH;
}

/*
 * field for a page number
 */
int gotopage(int c, struct position *position, struct output *output) {
	static char gotopagestring[100] = "";
	int res;
	double n;

	switch (c) {
	case KEY_INIT:
		c = KEY_REDRAW;
		break;
	case KEY_PPAGE:
	case KEY_NPAGE:
		c = c == KEY_PPAGE ? KEY_UP : KEY_DOWN;
		break;
	case 'c':
		sprintf(gotopagestring, "%d", position->npage + 1);
		c = KEY_REDRAW;
		break;
	case 'e':
		sprintf(gotopagestring, "%d", position->totpages);
		c = KEY_REDRAW;
		break;
	}

	n = position->npage + 1;
	res = number(c == KEY_INIT ? KEY_REDRAW : c, output,
		"go to page: ", gotopagestring, NULL,
		"c=current e=end up=previous down=next enter=go",
		&n, 1, position->totpages);
	if (res == -2) {
		number(KEY_REDRAW, output,
			"go to page: ", gotopagestring, "no such page",
			"c=current e=end up=previous down=next enter=go",
			&n, 1, position->totpages);
		return WINDOW_GOTOPAGE;
	}
	if (res == 1) {
		gotopagestring[0] = '\0';
		position->npage = n - 1;
		readpage(position, output);
		firsttextbox(position, output);
	}
	return res ? WINDOW_DOCUMENT : WINDOW_GOTOPAGE;
}

/*
 * field for minimal width
 */
int minwidth(int c, struct position *position, struct output *output) {
	static char minwidthstring[100] = "";
	int res;

	res = number(c, output, "minimal width: ", minwidthstring, NULL,
		"down=increase enter=decrease", &output->minwidth, 0, 1000);
	if (res == 1) {
		readpage(position, output);
		firsttextbox(position, output);
	}
	return res ? WINDOW_DOCUMENT : WINDOW_WIDTH;
}

/*
 * field for text distance
 */
int textdistance(int c, struct position *position, struct output *output) {
	static char distancestring[100] = "";
	int res;

	res = number(c, output, "text distance: ", distancestring, NULL,
		"down=increase enter=decrease", &output->distance, 0, 1000);
	if (res == 1) {
		readpage(position, output);
		firsttextbox(position, output);
	}
	return res ? WINDOW_DOCUMENT : WINDOW_DISTANCE;
}

/*
 * window selector
 */
int selectwindow(int window, int c,
		struct position *position, struct output *output) {
	switch (window) {
	case WINDOW_DOCUMENT:
		return document(c, position, output);
	case WINDOW_HELP:
		return help(c, position, output);
	case WINDOW_TUTORIAL:
		return tutorial(c, position, output);
	case WINDOW_MENU:
		return menu(c, position, output);
	case WINDOW_GOTOPAGE:
		return gotopage(c, position, output);
	case WINDOW_SEARCH:
		return search(c, position, output);
	case WINDOW_VIEWMODE:
		return viewmode(c, position, output);
	case WINDOW_FITDIRECTION:
		return fitdirection(c, position, output);
	case WINDOW_WIDTH:
		return minwidth(c, position, output);
	case WINDOW_DISTANCE:
		return textdistance(c, position, output);
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * show an arbitrary label
 */
void label(struct output *output, char *string, int bottom) {
	double width, x, y;

	cairo_identity_matrix(output->cr);

	width = strlen(string) * output->extents.max_x_advance;
	x = (output->dest.x2 + output->dest.x1) / 2 - width / 2;
	y = output->dest.y2 - bottom * (output->extents.height + 20.0 + 2.0);

	cairo_set_source_rgb(output->cr, 0, 0, 0);
	cairo_rectangle(output->cr,
		x - 10.0, y - 20.0,
		width + 20.0, output->extents.height + 20.0);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_move_to(output->cr, x, y - 10.0 + output->extents.ascent);
	cairo_show_text(output->cr, string);

	cairo_stroke(output->cr);
}

/*
 * show some help
 */
void helplabel(struct position *position, struct output *output) {
	(void) position;

	if (output->help[0] == '\0')
		return;

	label(output, output->help, 1);

	if (output->timeout == 0)
		output->timeout = 1200;
	output->help[0] = '\0';
}

/*
 * show the current page number
 */
void pagenumber(struct position *position, struct output *output) {
	static int prev = -1;
	char s[100];

	if (position->npage == prev && ! output->pagenumber)
		return;

	if (output->totalpages)
		sprintf(s, "page %d of %d",
			position->npage + 1, position->totpages);
	else
		sprintf(s, "page %d", position->npage + 1);
	label(output, s, 2);

	if (output->timeout == 0)
		output->timeout = 1200;
	output->pagenumber = FALSE;
	prev = position->npage;
}

/*
 * show the current mode
 */
void showmode(struct position *position, struct output *output) {
	static int prev = -1;
	char s[100];
	char *modes[] = {"textarea", "boundingbox", "page"};

	(void) position;

	if (output->viewmode == prev && ! output->showmode)
		return;

	sprintf(s, "viewmode: %s", modes[output->viewmode]);
	label(output, s, 3);

	if (output->timeout == 0)
		output->timeout = 1200;
	output->showmode = FALSE;
	prev = output->viewmode;
}

/*
 * show the current fit direction
 */
void showfit(struct position *position, struct output *output) {
	static int prev = -1;
	char s[100];
	char *fits[] = {"both", "horizontal", "vertical"};

	(void) position;

	if (output->fit == prev && ! output->showfit)
		return;

	sprintf(s, "fit: %s", fits[output->fit]);
	label(output, s, 4);

	if (output->timeout == 0)
		output->timeout = 1200;
	output->showfit = FALSE;
	prev = output->fit;
}

/*
 * show the filename
 */
void filename(struct position *position, struct output *output) {
	char s[100];

	if (! output->filename)
		return;

	sprintf(s, "%s", position->filename);
	label(output, s, 5);

	if (output->timeout == 0)
		output->timeout = 1200;
	output->filename = FALSE;
}

/*
 * draw a list of rectangles in pdf points
 */
void selection(struct position *position, struct output *output, GList *s) {
	double width, height;
	GList *l;
	PopplerRectangle *r;

	cairo_save(output->cr);
	cairo_scale(output->cr, 1, -1);
	poppler_page_get_size(position->page, &width, &height);
	cairo_translate(output->cr, 0, -height);
	cairo_set_source_rgb(output->cr, 0.3, 0.0, 0.3);
	cairo_set_operator(output->cr, CAIRO_OPERATOR_DIFFERENCE);
	for (l = s; l != NULL; l = l->next) {
		r = (PopplerRectangle *) l->data;
		cairo_rectangle(output->cr,
			r->x1, r->y1, r->x2 - r->x1, r->y2 - r->y1);
		cairo_fill(output->cr);
	}
	cairo_stroke(output->cr);
	cairo_restore(output->cr);
}

/*
 * draw the document with the labels on top
 */
void draw(struct cairofb *cairofb,
		struct position *position, struct output *output) {
	if (vt_suspend)
		return;

	if (output->redraw) {
		cairofb_clear(cairofb, 1.0, 1.0, 1.0);
		moveto(position, output);
		poppler_page_render(position->page, output->cr);
		rectangle_draw(output->cr,
			&position->textarea->rect[position->box],
			FALSE, FALSE, TRUE);
		selection(position, output, output->found);
		output->redraw = FALSE;
	}

	helplabel(position, output);
	pagenumber(position, output);
	showmode(position, output);
	showfit(position, output);
	filename(position, output);

	cairofb_flush(cairofb);
}

/*
 * read a character from input
 */
int input(int timeout) {
	fd_set fds;
	int max, ret;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	max = STDIN_FILENO;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;

	ret = select(max + 1, &fds, NULL, NULL, timeout != 0 ? &tv : NULL);

	if (ret == -1) {
		if (vt_redraw) {
			vt_redraw = FALSE;
			return KEY_REDRAW;
		}
		else
			return KEY_SIGNAL;
	}

	if (FD_ISSET(STDIN_FILENO, &fds))
		return getch();

	return KEY_TIMEOUT;
}

/*
 * open a pdf file
 */
struct position *openpdf(char *filename) {
	char *uri;
	struct position *position;

	position = malloc(sizeof(struct position));

	position->filename = strdup(filename);

	uri = filenametouri(position->filename);
	if (uri == NULL)
		exit(EXIT_FAILURE);
	position->doc = poppler_document_new_from_file(uri, NULL, NULL);
	free(uri);
	if (position->doc == NULL) {
		printf("error opening pdf file\n");
		return NULL;
	}

	position->totpages = poppler_document_get_n_pages(position->doc);
	if (position->totpages < 1) {
		printf("no page in document\n");
		return NULL;
	}

	return position;
}

/*
 * close a pdf file
 */
void closepdf(struct position *position) {
	free(position->filename);
	free(position);
}

/*
 * index of a character in a string
 */
int optindex(char arg, char *all) {
	char *pos;
	pos = index(all, arg);
	if (pos == NULL)
		return -1;
	return pos - all;
}

/*
 * scan a fraction like w:h or n/d
 */
double fraction(char *arg) {
	char *col;
	col = index(arg, ':');
	if (col == NULL) {
		col = index(arg, '/');
		if (col == NULL)
			return atof(arg);
	}
	*col = '\0';
	return atof(arg) / atof(col + 1);
}

/*
 * usage
 */
void usage() {
	printf("pdf viewer with automatic zoom to text\n");
	printf("usage:\n\thovacui\t[-m viewmode] [-f direction] ");
	printf("[-w minwidth] [-t distance]\n");
	printf("\t\t[-s aspect] [-d device] file.pdf\n");
	printf("\t\t-m viewmode\tzoom to: text, boundingbox, page\n");
	printf("\t\t-f direction\tfit: horizontally, vertically, both\n");
	printf("\t\t-w minwidth\tminimal width (maximal zoom)\n");
	printf("\t\t-t distance\tminimal text distance\n");
	printf("\t\t-s aspect\tthe screen aspect (e.g., 4:3)\n");
	printf("\t\t-d device\tfbdev device, default /dev/fb0\n");
	printf("main keys: 'h'=help 'g'=go to page '/'=search 'q'=quit ");
	printf("'m'=menu\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char configfile[4096], configline[1000], s[1000];
	FILE *config;
	double d;
	char *fbdev;
	struct cairofb *cairofb;
	double margin;
	double fontsize;
	char *filename;
	struct position *position;
	struct output output;
	double screenaspect;
	int opt;
	int firstwindow;
	int noinitlabels;

	WINDOW *w;
	int c;
	int window, next;

				/* defaults */

	output.viewmode = 0;
	output.totalpages = FALSE;
	output.fit = 1;
	output.minwidth = -1;
	output.distance = 15.0;
	output.scroll = 1.0 / 4.0;
	screenaspect = -1;
	firstwindow = WINDOW_TUTORIAL;
	margin = 10.0;
	fontsize = 16.0;
	fbdev = "/dev/fb0";
	noinitlabels = FALSE;

				/* config file */

	snprintf(configfile, 4096, "%s/.config/hovacui/hovacui.conf",
		getenv("HOME"));
	config = fopen(configfile, "r");
	if (config != NULL) {
		while (fgets(configline, 900, config)) {
			if (configline[0] == '#')
				continue;
			if (sscanf(configline, "mode %s", s) == 1)
				output.viewmode = optindex(optarg[0], "tbp");
			if (sscanf(configline, "fit %s", s) == 1)
				output.fit = optindex(optarg[0], "bhv");
			if (sscanf(configline, "minwidth %lg", &d) == 1)
				output.minwidth = d;
			if (sscanf(configline, "distance %lg", &d) == 1)
				output.distance = d;
			if (sscanf(configline, "aspect %s", s) == 1)
				screenaspect = fraction(s);
			if (sscanf(configline, "scroll %s", s) == 1)
				output.scroll = fraction(s);
			if (sscanf(configline, "fontsize %lg", &d) == 1)
				fontsize = d;
			if (sscanf(configline, "margin %lg", &d) == 1)
				margin = d;
			if (sscanf(configline, "device %s", s) == 1)
				fbdev = strdup(s);
			if (sscanf(configline, "%s", s) == 1 &&
			    ! strcmp(s, "notutorial"))
				firstwindow = WINDOW_DOCUMENT;
			if (sscanf(configline, "%s", s) == 1 &&
			    ! strcmp(s, "totalpages"))
				output.totalpages = TRUE;
			if (sscanf(configline, "%s", s) == 1 &&
			    ! strcmp(s, "noinitlabels"))
				noinitlabels = TRUE;
		}
		fclose(config);
	}

				/* cmdline arguments */

	while (-1 != (opt = getopt(argn, argv, "m:f:w:t:d:s:h")))
		switch (opt) {
		case 'm':
			output.viewmode = optindex(optarg[0], "tbp");
			if (output.viewmode == -1) {
				printf("unsupported mode: %s\n", optarg);
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			output.fit = optindex(optarg[0], "bhv");
			if (output.fit == -1) {
				printf("unsupported fit mode: %s\n", optarg);
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'w':
			output.minwidth = atof(optarg);
			if (output.minwidth < 0) {
				printf("error: negative minimal width\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			output.distance = atof(optarg);
			if (output.distance < 0) {
				printf("error: negative text distance\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'd':
			fbdev = optarg;
			break;
		case 's':
			screenaspect = fraction(optarg);
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		}

	if (argn - 1 < optind) {
		printf("file name missing\n");
		usage();
		exit(EXIT_FAILURE);
	}
	filename = argv[optind];

				/* open input file */

	position = openpdf(filename);
	if (position == NULL)
		exit(EXIT_FAILURE);

				/* open fbdev as cairo */

	cairofb = cairofb_init(fbdev, 1);
	if (cairofb == NULL) {
		printf("cannot open %s as a cairo surface\n", fbdev);
		free(cairofb);
		exit(EXIT_FAILURE);
	}

				/* setup terminal */

	if (getenv("ESCDELAY") == NULL)
		setenv("ESCDELAY", "200", 1);
	w = initscr();
	cbreak();
	keypad(w, TRUE);
	noecho();
	curs_set(0);
	ungetch(KEY_INIT);
	getch();

	vt_setup();

				/* initialize position and output context */

	initposition(position);
	output.cr = cairofb->cr;

				/* initialize labels */

	if (noinitlabels || firstwindow != WINDOW_DOCUMENT) {
		output.dest.x1 = 0;
		output.dest.y1 = -500;
		output.dest.x2 = 200;
		output.dest.y2 = -100;
		output.redraw = FALSE;
		draw(cairofb, position, &output);
	}
	else
		output.filename = firstwindow == WINDOW_DOCUMENT;

				/* setup output */

	output.dest.x1 = margin;
	output.dest.y1 = margin;
	output.dest.x2 = cairofb->width - margin;
	output.dest.y2 = cairofb->height - margin;

	output.aspect = screenaspect == -1 ?
		1 : screenaspect * cairofb->height / cairofb->width;

	if (output.minwidth == -1)
		output.minwidth = (cairofb->width - 2 * margin) / 2;

	strcpy(output.search, "");
	output.found = NULL;

	output.timeout = 0;
	output.help[0] = '\0';
	output.help[79] = '\0';

	cairo_select_font_face(output.cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output.cr, fontsize);
	cairo_font_extents(output.cr, &output.extents);

				/* first window */

	window = document(KEY_INIT, position, &output);
	if (window != firstwindow) {
		draw(cairofb, position, &output);
		window = selectwindow(firstwindow, KEY_INIT,
				position, &output);
	}
	else if (! noinitlabels)
		strncpy(output.help, "press 'h' for help", 79);

				/* event loop */

	while (window != WINDOW_EXIT) {

					/* draw the document */

		draw(cairofb, position, &output);

					/* read input */

		c = input(output.timeout);
		output.timeout = 0;
		if (vt_suspend || c == KEY_SIGNAL)
			continue;
		if (c == KEY_REDRAW || c == KEY_TIMEOUT)
			draw(cairofb, position, &output);

					/* pass input to window */

		next = selectwindow(window, c, position, &output);
		if (next != window) {
			if (next != WINDOW_DOCUMENT)
				draw(cairofb, position, &output);
			window = next;
			selectwindow(window, KEY_INIT, position, &output);
		}

					/* flush output */

		if (output.flush)
			cairofb_flush(cairofb);
	}

				/* close */

	closepdf(position);
	cairofb_finish(cairofb);
	clear();
	refresh();
	endwin();
	return EXIT_SUCCESS;
}


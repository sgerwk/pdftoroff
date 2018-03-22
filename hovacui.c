/*
 * hovacui.c
 *
 * view a pdf document, autozooming to the text
 *
 * todo:
 * - man page
 * - separate file for gui stuff
 * - improve column-sorting rectangles (to be done in pdfrects.c)
 * - briefly show the new mode after 'm' or 'f'
 * - key to move to next/previous block of text
 * - cache the textarea list of pages already scanned
 * - save last position(s) to $(HOME)/.pdfpositions
 * - include images (in pdfrects.c)
 * - utf8 in dialog()
 * - key to reset viewmode and fit direction to initial values
 * - search:
 *	+ dialog (+paste)
 *	+ poppler_page_find_text()
 *	+ move to the textblock that contains the string,
 *	  set scrolly so that the string is at the center
 * - history of positions
 * - set output->redraw to FALSE when not moving
 * - note that horizontal fitting is intended for horizontal scripts,
 *   but the program also supports vertical fitting; yet, it only supports
 *   vertical scrolling
 * - order of rectangles for right-to-left and top-to-bottom scripts
 *   (generalize sorting function in pdfrects.c)
 * - man: KEY_LEFT and KEY_RIGHT work as next/prev box in horizontal fit, but
 *   they are regular scroll keys in vertical fit; and the other way around
 * - i18n
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
 * the same for position->scrollx
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

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

/*
 * the windows
 */
enum window {
	WINDOW_DOCUMENT,
	WINDOW_HELP,
	WINDOW_GOTOPAGE,
	WINDOW_SEARCH,
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

	/* decorations */
	int pagenumber;

	/* size of font */
	cairo_font_extents_t extents;
};

/*
 * a position within a document
 */
struct position {
	/* the poppler document */
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
 * read the current page
 */
int readpage(struct position *position, struct output *output) {
	position->page =
		poppler_document_get_page(position->doc, position->npage);

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
 * check whether the bounding box is all in the screen
 */
int boundingboxinscreen(struct position *position, struct output *output) {
	if (position->boundingbox->x2 - position->boundingbox->x1 >
	    xscreentodocdistance(output, output->dest.x2 - output->dest.x1))
		return FALSE;
	if (position->boundingbox->y2 - position->boundingbox->y1 >
	    yscreentodocdistance(output, output->dest.y2 - output->dest.y1))
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
	case 'h':
		output->redraw = FALSE;
		return WINDOW_HELP;
	case 'g':
		output->redraw = FALSE;
		return WINDOW_GOTOPAGE;
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
	case 'm':
		output->viewmode = (output->viewmode + 1) % 3;
		position->box = 0;
		position->scrollx = 0;
		position->scrolly = 0;
		readpage(position, output);
		break;
	case 'w':
		if (output->minwidth > 0)
			output->minwidth -= 10;
		break;
	case 'W':
		if (boundingboxinscreen(position, output))
			break;
		output->minwidth += 10;
		break;
	case 't':
	case 'T':
		output->distance += c == 't' ? -1 : 1;
		position->box = 0;
		position->scrollx = 0;
		position->scrolly = 0;
		readpage(position, output);
		break;
	case 'f':
		output->fit = (output->fit + 1) % 3;
		position->scrollx = 0;
		position->scrolly = 0;
		break;
	case 'l':
		output->pagenumber = TRUE;
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
 * show some text
 */
int text(int c, struct output *output, char *viewtext[], int *line) {
	double percent = 0.8;
	double width = output->dest.x2 - output->dest.x1;
	double height = output->dest.y2 - output->dest.y1;
	double marginx = width * (1 - percent) / 2;
	double marginy = height * (1 - percent) / 2;
	double borderx = 10.0;
	double bordery = 10.0;
	double textheight;
	int n, l, lines;

	for (n = 0; viewtext[n] != NULL; n++) {
	}

	cairo_identity_matrix(output->cr);
	lines = (int) (height * percent - bordery * 2) /
		(int) output->extents.height;
	textheight = lines * output->extents.height;
	height = textheight + 2 * bordery;

	switch (c) {
	case KEY_DOWN:
		if (*line >= n - lines)
			return 0;
		(*line)++;
		output->redraw = TRUE;
		break;
	case KEY_UP:
		if (*line <= 0)
			return 0;
		(*line)--;
		output->redraw = TRUE;
		break;
	case KEY_INIT:
	case KEY_REDRAW:
	case KEY_TIMEOUT:
		break;
	default:
		output->redraw = TRUE;
		return -1;
	}

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + marginy,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		height);
	cairo_fill(output->cr);
	cairo_stroke(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_save(output->cr);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx + borderx,
		output->dest.y1 + marginy + bordery,
		output->dest.x2 - output->dest.x1 - marginx * 2 - borderx * 2,
		textheight);
	cairo_clip(output->cr);

	cairo_translate(output->cr, 0.0, - output->extents.height * *line);
	cairo_move_to(output->cr,
		output->dest.x1 + marginx + borderx,
		output->dest.y1 + marginy + bordery + output->extents.ascent);
	for (l = 0; viewtext[l] != NULL; l++)
		printline(output->cr, viewtext[l], output->extents.height);
	cairo_stroke(output->cr);
	cairo_restore(output->cr);

	if (lines < n) {
		cairo_rectangle(output->cr,
			output->dest.x2 - marginx - borderx,
			output->dest.y1 + marginy +
				*line / (double) n * height,
			borderx,
			lines / (double) n * height);
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
		"------------------------------------------",
		"PageUp     previous page",
		"PageDown   next page",
		"Home       top of page",
		"End        bottom of page",
		"m          rotate among view modes:",
		"           textarea, boundingbox, page",
		"f          change fitting direction:",
		"           horizontal, vertical, both",
		"w/W        + or - minimal viewbox width",
		"           (determines the maximal zoom)",
		"g          go to page",
		"h          help",
		"q          quit",
		"",
		"any key to continue",
		NULL
	};
	static int line = 0;
	(void) position;

	return text(c, output, helptext, &line) == 0 ?
		WINDOW_HELP : WINDOW_DOCUMENT;
}

/*
 * generic dialog
 */
void dialog(int c, struct output *output,
		char *label, char *current, char *error) {
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (output->dest.x2 - output->dest.x1) * prop;
	int l;

	l = strlen(current);
	if (c == KEY_BACKSPACE || c == KEY_DC) {
		if (l < 0)
			return;
		current[l - 1] = '\0';
	}
	else if (c >= '0' && c <= '9') {
		if (l > 50)
			return;
		current[l] = c;
		current[l + 1] = '\0';
	}
	else if (c != KEY_INIT && c != KEY_REDRAW)
		return;

	cairo_identity_matrix(output->cr);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + 20,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		output->extents.height + 10);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		output->dest.x1 + marginx + output->extents.height / 2,
		output->dest.y1 + 20 + 5 + output->extents.ascent);
	cairo_show_text(output->cr, label);
	cairo_show_text(output->cr, current);
	cairo_show_text(output->cr, "_ ");
	cairo_show_text(output->cr, error);
}

/*
 * dialog for a page number
 */
int gotopage(int c, struct position *position, struct output *output) {
	static char gotostring[100] = "";
	int n;

	if (c == '\033' || c == 'q')
		return WINDOW_DOCUMENT;

	if (c != KEY_ENTER && c != '\n' &&
	   (c != 'e' || gotostring[0] != '\0')) {
		dialog(c, output, "go to page: ", gotostring, "");
		output->flush = TRUE;
		output->pagenumber = TRUE;
		return WINDOW_GOTOPAGE;
	}

	if (c != 'e' && gotostring[0] == '\0')
		return WINDOW_DOCUMENT;

	n = c == 'e' ? position->totpages : atoi(gotostring);

	if (n == position->npage + 1) {
		gotostring[0] = '\0';
		return WINDOW_DOCUMENT;
	}

	if (n < 1 || n > position->totpages) {
		dialog(KEY_INIT, output,
			"go to page: ", gotostring, "[no such page]");
		output->flush = TRUE;
		return WINDOW_GOTOPAGE;
	}

	position->npage = n - 1;
	readpage(position, output);
	firsttextbox(position, output);
	gotostring[0] = '\0';
	return WINDOW_DOCUMENT;
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
	case WINDOW_GOTOPAGE:
		return gotopage(c, position, output);
	case WINDOW_SEARCH:
		return WINDOW_DOCUMENT;
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * show an arbitrary label
 */
void label(struct output *output, char *string, double bottom) {
	double width, x, y;

	cairo_identity_matrix(output->cr);

	width = strlen(string) * output->extents.max_x_advance;
	x = (output->dest.x2 + output->dest.x1) / 2 - width / 2;
	y = output->dest.y2 - bottom - output->extents.height;

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
 * show the current page number
 */
void pagenumber(struct position *position, struct output *output) {
	static int prev = -1;
	char pagenum[100];

	if (position->npage == prev && ! output->pagenumber)
		return;

	sprintf(pagenum, "page %d", position->npage + 1);
	label(output, pagenum, 20.0);

	output->timeout = TRUE;
	output->pagenumber = FALSE;
	prev = position->npage;
}

/*
 * draw the document with the decorations on top
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
	}

	pagenumber(position, output);

	cairofb_flush(cairofb);
}

/*
 * read a character from input
 */
int input(int timeout) {
	fd_set fds;
	int max, ret;
	struct timeval tv = {1, 400};

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	max = STDIN_FILENO;

	ret = select(max + 1, &fds, NULL, NULL, timeout ? &tv : NULL);

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
 * scan an aspect ratio w:h
 */
double aspect(char *arg) {
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
	printf("fbdev pdf viewer with automatic zoom to text\n");
	printf("usage:\n\thovacui\t[-m viewmode] [-f direction] ");
	printf("[-w minwidth] [-t distance]\n");
	printf("\t\t[-s aspect] [-d device] file.pdf\n");
	printf("\t\t-m viewmode\tzoom to: text, boundingbox, page\n");
	printf("\t\t-f direction\tfit: horizontally, vertically, both\n");
	printf("\t\t-w minwidth\tminimal width, determine maximal zoom\n");
	printf("\t\t-t distance\tminimal text distance\n");
	printf("\t\t-s aspect\tthe screen aspect (e.g., 4:3)\n");
	printf("\t\t-d device\tfbdev device, default /dev/fb0\n");
	printf("keys:\t'h'=help 'g'=go to page 'q'=quit\n");
	printf("\t'm'=change view mode 'f'=change fit direction\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char configfile[4096], configline[1000], s[1000];
	FILE *config;
	char *filename, *uri;
	double d;
	char *fbdev = "/dev/fb0";
	struct cairofb *cairofb;
	double margin = 10.0;
	double fontsize = 16.0;
	struct position position;
	struct output output;
	double screenaspect;
	int opt;

	WINDOW *w;
	int c;
	int window, next;

				/* defaults */

	output.viewmode = 0;
	output.fit = 1;
	output.minwidth = -1;
	output.distance = 15.0;
	output.scroll = 1.0 / 4.0;
	screenaspect = -1;

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
				screenaspect = aspect(s);
			if (sscanf(configline, "scroll %s", s) == 1)
				output.scroll = aspect(s);
			if (sscanf(configline, "device %s", s) == 1)
				fbdev = strdup(s);
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
			screenaspect = aspect(optarg);
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

	uri = filenametouri(filename);
	if (uri == NULL)
		exit(EXIT_FAILURE);
	position.doc = poppler_document_new_from_file(uri, NULL, NULL);
	free(uri);
	if (position.doc == NULL) {
		printf("error opening pdf file\n");
		exit(EXIT_FAILURE);
	}

	position.totpages = poppler_document_get_n_pages(position.doc);
	if (position.totpages < 1) {
		printf("no page in document\n");
		exit(EXIT_FAILURE);
	}

				/* open fbdev as cairo */

	cairofb = cairofb_init(fbdev, 1);
	if (cairofb == NULL) {
		printf("cannot open %s as a cairo surface\n", fbdev);
		free(cairofb);
		exit(EXIT_FAILURE);
	}

				/* setup terminal */

	w = initscr();
	cbreak();
	keypad(w, TRUE);
	noecho();
	curs_set(0);
	ungetch(KEY_INIT);
	getch();

	vt_setup();

				/* initialize position and output */

	initposition(&position);

	output.cr = cairofb->cr;

	output.redraw = TRUE;

	output.dest.x1 = margin;
	output.dest.y1 = margin;
	output.dest.x2 = cairofb->width - margin;
	output.dest.y2 = cairofb->height - margin;

	output.aspect = screenaspect == -1 ?
		1 : screenaspect * cairofb->height / cairofb->width;

	if (output.minwidth == -1)
		output.minwidth = (cairofb->width - 2 * margin) / 2;

	output.timeout = TRUE;
	output.pagenumber = TRUE;

	cairo_select_font_face(output.cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output.cr, fontsize);
	cairo_font_extents(output.cr, &output.extents);

				/* event loop */

	window = document(KEY_INIT, &position, &output);

	while (window != WINDOW_EXIT) {

					/* draw the document */

		draw(cairofb, &position, &output);

					/* read input */

		c = input(output.timeout);
		if (vt_suspend || c == KEY_SIGNAL)
			continue;
		if (c == KEY_REDRAW || c == KEY_TIMEOUT)
			draw(cairofb, &position, &output);

					/* pass input to window */

		next = selectwindow(window, c, &position, &output);
		if (next != window) {
			window = next;
			selectwindow(window, KEY_INIT, &position, &output);
		}

					/* flush output */

		if (output.flush)
			cairofb_flush(cairofb);
	}

				/* close */

	cairofb_finish(cairofb);
	clear();
	refresh();
	endwin();
	return EXIT_SUCCESS;
}


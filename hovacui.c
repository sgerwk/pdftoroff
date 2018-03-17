/*
 * hovacui.c
 *
 * view a pdf document, autozooming to the text
 *
 * todo:
 * - man page
 * - vt switching
 * - fb device parametric (ex. -d /dev/fb1)
 * - console clean on exit
 * - improve column-sorting rectangles (to be done in pdfrects.c)
 * - briefly show the page number in a corner when changing page
 * - key to move to next/previous block of text
 * - cache the textarea list of pages already scanned
 * - save last position(s) to $(HOME)/.pdfpositions
 * - include images (in pdfrects.c)
 * - change output.distance by option (-t) and by keys ('t'/'T')
 * - utf8 in dialog()
 * - turn help() into a generic text viewer window
 * - 'W' stops when the page (or the boundingbox) is all inside the screen
 * - allow fitting to page (like 'W' until maximal zoom)
 * - search:
 *	+ dialog (+paste)
 *	+ poppler_page_find_text()
 *	+ move to the textblock that contains the string,
 *	  set scrolly so that the string is at the center
 * - history of positions
 * - set output->redraw to FALSE when not moving
 * - note that horizontal fitting is intended for horizontal text
 * - i18n
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
 * special key that tells a window to initalize itself
 */
#define KEY_INIT 0

/*
 * output parameters
 */
struct output {
	/* the cairo surface */
	cairo_t *cr;

	/* the destination rectangle */
	PopplerRectangle dest;

	/* the minimal textbox-to-textbox distance */
	double distance;

	/* the minimal width; tells the maximal zoom */
	double minwidth;

	/* zoom to: 0=text, 1=boundingbox, 2=page */
	int viewmode;

	/* how much to scroll */
	int scroll;

	/* whether the document has to be redrawn */
	int redraw;

	/* whether the output is to be flushed */
	int flush;
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

	/* the vertical scroll within the rectangle currently viewed */
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
	position->scrolly = 0;
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
		position->boundingbox = poppler_rectangle_new();
		position->boundingbox->x1 = 0;
		position->boundingbox->y1 = 0;
		poppler_page_get_size(position->page,
			&position->boundingbox->x2,
			&position->boundingbox->y2);
		position->textarea = NULL;
		break;
	}
	if (position->textarea == NULL) {
		position->textarea = rectanglelist_new(1);
		rectanglelist_add(position->textarea, position->boundingbox);
	}

	return 0;
}

/*
 * translate y from viewbox coordinates to screen coordinates and back
 */
double doctoscreen(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_user_to_device(output->cr, &xx, &yy);
	return yy;
}
double screentodoc(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_device_to_user(output->cr, &xx, &yy);
	return yy;
}

/*
 * change scrolly to avoid empty space atop or below (prefer empty space below)
 */
void adjustscroll(struct position *position, struct output *output) {

	/* bottom of bounding box mapped to the screen over bottom of screen */
	if (doctoscreen(output, position->boundingbox->y2 - position->scrolly)
	    < output->dest.y2)
		position->scrolly = position->boundingbox->y2 -
			screentodoc(output, output->dest.y2);

	/* top of bounding box mapped to the screen below top of screen */
	if (doctoscreen(output, position->boundingbox->y1 - position->scrolly)
	    > output->dest.y1)
		position->scrolly = position->boundingbox->y1 -
			screentodoc(output, output->dest.y1);

	return;
}

/*
 * adjust width of viewbox to the minimum allowed
 */
void adjustviewbox(struct position *position, struct output *output) {
	double d;
	PopplerRectangle *viewbox;

	viewbox = position->viewbox;
	if (viewbox->x2 - viewbox->x1 < output->minwidth) {
		d = output->minwidth - viewbox->x2 + viewbox->x1;
		viewbox->x1 -= d / 2;
		viewbox->x2 += d / 2;
	}
}

/*
 * move to position
 */
void moveto(struct position *position, struct output *output) {
	cairo_identity_matrix(output->cr);

	poppler_rectangle_free(position->viewbox);
	position->viewbox = poppler_rectangle_copy
		(&position->textarea->rect[position->box]);
	adjustviewbox(position, output);
	
	rectangle_map_to_cairo(output->cr, &output->dest, position->viewbox,
		1, 0, 1);
	adjustscroll(position, output);

	cairo_translate(output->cr, 0, -position->scrolly);
}

/*
 * go to the top of current viewbox
 */
int topviewbox(struct position *position, struct output *output) {
	(void) output;
	position->scrolly = 0;
	return 0;
}

/*
 * go to the top of the first viewbox of the page
 */
int firstviewbox(struct position *position, struct output *output) {
	position->box = 0;
	return topviewbox(position, output);
}

/*
 * move to the start of the next page
 */
int nextpage(struct position *position, struct output *output) {
	if (position->npage + 1 >= position->totpages)
		return -1;

	position->npage++;
	readpage(position, output);
	return firstviewbox(position, output);
}

/*
 * scroll down
 */
int scrolldown(struct position *position, struct output *output) {
	moveto(position, output);
	if (doctoscreen(output, position->viewbox->y2) >
	    output->dest.y2 + 0.01) {
		position->scrolly += output->scroll;
		return 0;
	}

	if (position->box + 1 < position->textarea->num) {
		position->box++;
		return topviewbox(position, output);
	}

	return nextpage(position, output);
}

/*
 * move to the bottom of the current viewbox
 */
int bottomviewbox(struct position *position, struct output *output) {
	position->scrolly = 0;
	moveto(position, output);
	position->scrolly =
		MAX(0, position->viewbox->y2 -
			screentodoc(output, output->dest.y2));
	return 0;
}

/*
 * go to the bottom of the last viewbox in the page
 */
int lastviewbox(struct position *position, struct output *output) {
	position->box = position->textarea->num - 1;
	return bottomviewbox(position, output);
}

/*
 * move to the bottom of the previous page
 */
int prevpage(struct position *position, struct output *output) {
	if (position->npage <= 0)
		return -1;

	position->npage--;
	readpage(position, output);
	return lastviewbox(position, output);
}

/*
 * scroll up
 */
int scrollup(struct position *position, struct output *output) {
	moveto(position, output);
	if (doctoscreen(output, position->viewbox->y1) <
	    output->dest.y1 - 0.01) {
		position->scrolly -= output->scroll;
		return 0;
	}

	if (position->box - 1 >= 0) {
		position->box--;
		return bottomviewbox(position, output);
	}

	return prevpage(position, output);
}

/*
 * document window
 */
int document(int c, struct position *position, struct output *output) {
	output->redraw = TRUE;

	switch (c) {
	case KEY_INIT:
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
	case KEY_HOME:
		firstviewbox(position, output);
		break;
	case KEY_END:
		lastviewbox(position, output);
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
		position->scrolly = 0;
		readpage(position, output);
		break;
	case 'w':
		output->minwidth -= 10;
		break;
	case 'W':
		output->minwidth += 10;
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
 * help text
 */
char *helptext[] = {
	"hovacui - pdf viewer with autozoom to text",
	"------------------------------------------",
	"PageUp     previous page",
	"PageDown   next page",
	"Home       top of page",
	"End        bottom of page",
	"m          rotate among view modes:",
	"           textarea, boundingbox, page",
	"w/W        + or - minimal viewbox width",
	"           (determines the maximal zoom)",
	"g          go to page",
	"h          help",
	"q          quit",
	"",
	"any key to continue"
};

/*
 * help
 */
int help(int c, struct position *position, struct output *output) {
	(void) position;
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (output->dest.x2 - output->dest.x1) * prop;
	double marginy = (output->dest.y2 - output->dest.y1) * prop;
	double fontsize = 18.0;
	unsigned l;

	if (c != KEY_INIT) {
		output->redraw = TRUE;
		return WINDOW_DOCUMENT;
	}

	cairo_identity_matrix(output->cr);
	cairo_select_font_face(output->cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output->cr, fontsize);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + marginy,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		output->dest.y2 - output->dest.y1 - marginy * 2);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		output->dest.x1 + marginx + 10.0,
		output->dest.y1 + marginy + 10.0 + fontsize);
	for (l = 0; l < sizeof(helptext) / sizeof(char *); l++)
		printline(output->cr, helptext[l], fontsize);
	cairo_stroke(output->cr);

	output->redraw = FALSE;
	output->flush = TRUE;
	return WINDOW_HELP;
}

/*
 * generic dialog
 */
void dialog(int c, struct output *output,
		char *label, char *current, char *error) {
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (output->dest.x2 - output->dest.x1) * prop;
	double fontsize = 18.0;
	cairo_font_extents_t extents;
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
	else if (c != KEY_INIT)
		return;

	cairo_identity_matrix(output->cr);
	cairo_select_font_face(output->cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output->cr, fontsize);

	cairo_font_extents(output->cr, &extents);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + 20,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		extents.height + 10);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		output->dest.x1 + marginx + fontsize / 2,
		output->dest.y1 + 20 + 5 + extents.ascent);
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
	firstviewbox(position, output);
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
 * draw the document
 */
void draw(struct cairofb *cairofb,
		struct position *position, struct output *output) {
	cairofb_clear(cairofb, 1.0, 1.0, 1.0);
	moveto(position, output);
	poppler_page_render(position->page, output->cr);
	rectangle_draw(output->cr,
		&position->textarea->rect[position->box],
		FALSE, FALSE, TRUE);
	cairofb_flush(cairofb);
}

/*
 * usage
 */
void usage() {
	printf("fbdev pdf viewer with automatic zoom to text\n");
	printf("usage:\n\thovacui [-m viewmode] [-w minwidth] file.pdf\n");
	printf("\t\t-m viewmode\tzoom to: 0=text, 1=boundingbox, 2=page\n");
	printf("\t\t-w minwidth\tminimal width, determine maximal zoom\n");
	printf("keys: 'k'=help 'g'=go to page 'q'=quit ");
	printf("'m'=change view mode\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char *filename, *uri;
	char *devname = "/dev/fb0";
	struct cairofb *cairofb;
	double margin = 10.0;
	struct position position;
	struct output output;
	int opt;

	WINDOW *w;
	int c;
	int window, next;

				/* arguments */

	output.viewmode = 0;
	output.minwidth = -1;

	while (-1 != (opt = getopt(argn, argv, "m:w:h")))
		switch (opt) {
		case 'm':
			output.viewmode = atoi(optarg);
			if (output.viewmode < 0 || output.viewmode >= 3) {
				printf("unsupported mode: %s\n", optarg);
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

	cairofb = cairofb_init(devname, 1);
	if (cairofb == NULL) {
		printf("cannot open %s as a cairo terminal\n", devname);
		free(cairofb);
		exit(EXIT_FAILURE);
	}

				/* init ncurses */

	w = initscr();
	cbreak();
	keypad(w, TRUE);
	noecho();
	curs_set(0);
	ungetch(0);

				/* initialize position and output */

	initposition(&position);

	output.cr = cairofb->cr;
	output.distance = 15.0;
	output.scroll = 50.0;

	output.redraw = 1;

	output.dest.x1 = margin;
	output.dest.y1 = margin;
	output.dest.x2 = cairofb->width - margin;
	output.dest.y2 = cairofb->height - margin;

	if (output.minwidth == -1)
		output.minwidth = (cairofb->width - 2 * margin) / 2;

				/* event loop */

	window = document(KEY_INIT, &position, &output);

	while (window != WINDOW_EXIT) {

					/* draw the document */

		if (output.redraw)
			draw(cairofb, &position, &output);

					/* process input */

		c = getch();
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
	endwin();
	return EXIT_SUCCESS;
}


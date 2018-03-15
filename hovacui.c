/*
 * hovacui.c
 *
 * view a pdf document, autozooming to the text
 *
 * todo:
 * - man page
 * - vt switching
 * - console clean on exit
 * - improve column-sorting rectangles (to be done in pdfrects.c)
 * - key to move to next/previous block of text
 * - cache the textarea list of pages already scanned
 * - save last position(s) to $(HOME)/.pdfpositions
 * - include images (in pdfrects.c)
 * - change output.distance by option and by key
 * - help window
 * - go to page (dialog)
 * - search:
 *	+ dialog
 *	+ move to the textblock that contains the string,
 *	  set scrolly so that the string is at the center
 * - history of positions
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <poppler.h>
#include <ncurses.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include "cairofb.h"
#include "pdfrects.h"

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

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
	/* even for the space atop, this is not simply checking
	 * position->scrolly<0, which is only relative to the current
	 * viewbox; it requires calculating whether position->scrolly is
	 * negative relative to the whole page, or to its boundingbox */

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
int scrolldown(struct position *position, struct output *output, double dy) {
	moveto(position, output);
	if (doctoscreen(output, position->viewbox->y2) >
	    output->dest.y2 + 0.01) {
		position->scrolly += dy;
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
int scrollup(struct position *position, struct output *output, double dy) {
	moveto(position, output);
	if (doctoscreen(output, position->viewbox->y1) <
	    output->dest.y1 - 0.01) {
		position->scrolly -= dy;
		return 0;
	}

	if (position->box - 1 >= 0) {
		position->box--;
		return bottomviewbox(position, output);
	}

	return prevpage(position, output);
}

/*
 * usage
 */
void usage() {
	printf("fbdev pdf viewer with automatic zoom to text\n");
	printf("usage:\n\thovacui [-m viewmode] [-w minwidth] file.pdf\n");
	printf("\t\t-m viewmode\tzoom to: 0=text, 1=boundingbox, 2=page\n");
	printf("\t\t-w minwidth\tminimal width, determine maximal zoom\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	char *filename, *uri;
	char *devname = "/dev/fb0";
	struct cairofb *cairofb;
	double margin = 10.0;
	double scroll = 50.0;
	struct position position;
	struct output output;
	int opt;

	WINDOW *w;
	int c;
	int stayinloop;

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
	ungetch('r');

				/* initialize position and output */

	initposition(&position);

	output.cr = cairofb->cr;
	output.distance = 15.0;

	output.dest.x1 = margin;
	output.dest.y1 = margin;
	output.dest.x2 = cairofb->width - margin;
	output.dest.y2 = cairofb->height - margin;

	if (output.minwidth == -1)
		output.minwidth = (cairofb->width - 2 * margin) / 2;

				/* event loop */

	for (stayinloop = 1; stayinloop; ) {

					/* read input */

		c = getch();
		switch (c) {
		case 'r':
			readpage(&position, &output);
			break;
		case 'q':
			stayinloop = 0;
			break;
		case KEY_DOWN:
			scrolldown(&position, &output, scroll);
			break;
		case KEY_UP:
			scrollup(&position, &output, scroll);
			break;
		case KEY_HOME:
			firstviewbox(&position, &output);
			break;
		case KEY_END:
			lastviewbox(&position, &output);
			break;
		case KEY_NPAGE:
			nextpage(&position, &output);
			break;
		case KEY_PPAGE:
			prevpage(&position, &output);
			break;
		case 'm':
			output.viewmode = (output.viewmode + 1) % 3;
			position.box = 0;
			position.scrolly = 0;
			readpage(&position, &output);
			break;
		case 'w':
			output.minwidth -= 10;
			break;
		case 'W':
			output.minwidth += 10;
			break;
		default:
			continue;
		} 

					/* draw */

		cairofb_clear(cairofb, 1.0, 1.0, 1.0);
		moveto(&position, &output);
		poppler_page_render(position.page, output.cr);
		rectangle_draw(output.cr,
			&position.textarea->rect[position.box],
			FALSE, FALSE, TRUE);
		cairofb_flush(cairofb);
	}

				/* close */

	cairofb_finish(cairofb);
	endwin();
	return EXIT_SUCCESS;
}


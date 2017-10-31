/*
 * test program for pdfrects.c
 *
 * arguments:
 *	-f		first page
 *	-l		last page
 *	-b		bounding box
 *	-d distance	minimal size of a white space
 *	-r level	the debugtextrectangles variables (-1 - 5, see below)
 *	-n		draw also the number of each rectangle
 *	-s		sort rectangles
 *	-a		test adding a new 100x100 rectangle in a free area
 *	file.pdf	file to read; output is always result.pdf
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>

#include "pdfrects.h"

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	gdouble distance = 15.0;
	gboolean numbers = FALSE;
	gboolean bb = FALSE;
	gboolean add = FALSE;
	gboolean sort = FALSE;
	int first = -1, last = -1;
	char *filename;

	gdouble width = 595.22, height = 842.00;

	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	RectangleList *textarea = NULL, *singlechars;
	PopplerRectangle *boundingbox = NULL;
	PopplerRectangle wholepage = {0.0, 0.0, width, height};

	PopplerRectangle insert = {200.0, 200.0, 300.0, 300.0}, moved;
	gboolean fits;

	cairo_surface_t *surface;
	cairo_t *cr;

				/* arguments */

	while ((opt = getopt(argc, argv, "f:l:nsbd:r:a")) != -1)
		switch(opt) {
		case 'f':
			first = atoi(optarg);
			break;
		case 'l':
			last = atoi(optarg);
			break;
		case 'n':
			numbers = TRUE;
			break;
		case 's':
			sort = TRUE;
			break;
		case 'b':
			bb = TRUE;
			break;
		case 'd':
			distance = atof(optarg);
			break;
		case 'r':
			debugtextrectangles = atoi(optarg);
			break;
		case 'a':
			add = TRUE;
			break;
		}

	if (argc - 1 < optind) {
		printf("pdfrects [-f page] [-l page] ");
		printf("[-b] [-d distance] [-r level] [-n] [-a] ");
		printf("file.pdf\n");
		exit(EXIT_FAILURE);
	}
	filename = filenametouri(argv[optind]);
	if (! filename)
		exit(EXIT_FAILURE);
	printf("%s -> result.pdf\n", argv[optind]);

				/* open file */

	doc = poppler_document_new_from_file(filename, NULL, NULL);
	if (doc == NULL) {
		printf("error opening pdf file\n");
		exit(EXIT_FAILURE);
	}

				/* pages */

	npages = poppler_document_get_n_pages(doc);
	if (npages < 1) {
		printf("no page in document\n");
		exit(EXIT_FAILURE);
	}
	if (first == -1)
		first = 0;
	if (first < 0 || first >= npages) {
		printf("no such first page: %d\n", first);
		printf("number of pages is %d\n", npages);
		exit(EXIT_FAILURE);
	}
	if (last == -1)
		last = npages - 1;
	if (last < 0 || last >= npages) {
		printf("no such last page: %d\n", last);
		printf("number of pages is %d\n", npages);
		exit(EXIT_FAILURE);
	}

				/* copy to destination */

	surface = cairo_pdf_surface_create("result.pdf", width, height);

	for (n = first; n <= last; n++) {
		printf("page %d\n", n);
		page = poppler_document_get_page(doc, n);

		if (bb)
			boundingbox = rectanglelist_boundingbox(page);
		else {
			textarea = rectanglelist_textarea_distance(page,
					distance);
			if (sort)
				rectanglelist_sort(textarea);
		}

		if (add) {
			singlechars = rectanglelist_characters(page);
			fits = rectanglelist_place(&wholepage,
					singlechars, &insert, &moved);
			rectanglelist_free(singlechars);
		}

		cr = cairo_create(surface);
		poppler_page_render_for_printing(page, cr);
		if (bb)
			rectangle_draw(cr, boundingbox, FALSE);
		else
			rectanglelist_draw(cr, textarea, FALSE, numbers);
		if (fits)
			rectangle_draw(cr, &moved, TRUE);
		cairo_destroy(cr);
		cairo_surface_show_page(surface);

		rectanglelist_free(textarea);
		poppler_rectangle_free(boundingbox);
	}

	cairo_surface_destroy(surface);

	return EXIT_SUCCESS;
}


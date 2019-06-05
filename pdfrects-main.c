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
	gboolean usage = FALSE;
	gdouble distance = 15.0;
	gboolean numbers = FALSE;
	gboolean bb = FALSE;
	gboolean painted = FALSE;
	gboolean add = FALSE;
	int sort = -1;
	int first = -1, last = -1;
	char *infile, *outfile;

	gdouble width = 595.22, height = 842.00;

	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	RectangleList *textarea = NULL, *singlechars;
	PopplerRectangle *boundingbox = NULL;
	PopplerRectangle wholepage = {0.0, 0.0, width, height};
	void (*order[])(RectangleList *, PopplerPage *) = {
		rectanglelist_quicksort,
		rectanglelist_twosort,
		rectanglelist_charsort
	};

	PopplerRectangle insert = {200.0, 200.0, 300.0, 300.0}, moved;
	gboolean fits = FALSE;

	cairo_surface_t *surface;
	cairo_t *cr;

				/* arguments */

	while ((opt = getopt(argc, argv, "f:l:nps:bd:r:ah")) != -1)
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
		case 'p':
			painted = TRUE;
			break;
		case 's':
			sort = atoi(optarg);
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
		case 'h':
			usage = TRUE;
			break;
		}

	if (! usage && argc - 1 < optind) {
		printf("input file name missing\n");
		usage = TRUE;
	}
	if (usage) {
		printf("usage:\n");
		printf("\tpdfrects [-f page] [-l page] ");
		printf("[-b] [-d distance] [-n [-s n]]\n");
		printf("\t         [-a] [-r level] [-h] ");
		printf("file.pdf\n");
		printf("\t\t-f page\t\tfirst page\n");
		printf("\t\t-l page\t\tlast page\n");
		printf("\t\t-b\t\tbounding box instead of textarea\n");
		printf("\t\t-d distance\tminimal distance of text boxes\n");
		printf("\t\t-n\t\tnumber boxes\n");
		printf("\t\t-p\t\tuse painted squares instead of text\n");
		printf("\t\t-s n\t\tsort boxes by method n\n");
		printf("\t\t-a\t\tadd a test box\n");
		printf("\t\t-r level\tdebug textarea algorithm\n");
		printf("\t\t-h\t\tthis help\n");
		exit(EXIT_FAILURE);
	}
	infile = filenametouri(argv[optind]);
	if (! infile)
		exit(EXIT_FAILURE);
	outfile = pdfaddsuffix(argv[optind], "boxes");

				/* open file */

	doc = poppler_document_new_from_file(infile, NULL, NULL);
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

	surface = cairo_pdf_surface_create(outfile, width, height);

	printf("infile: %s\n", argv[optind]);
	printf("outfile: %s\n", outfile);
	printf("pages: \n");

	for (n = first; n <= last; n++) {
		printf("  - page: %d\n", n);
		page = poppler_document_get_page(doc, n);

		if (bb) {
			boundingbox = painted ?
				rectanglelist_boundingbox_painted(page,
					distance) :
				rectanglelist_boundingbox(page);
			printf("    boundingbox:\n");
			rectangle_printyaml(stdout,
				"        ", "        ", boundingbox);
		}
		else {
			textarea = painted ?
				rectanglelist_paintedarea_distance(page,
					distance) :
				rectanglelist_textarea_distance(page,
					distance);
			if (sort >= 0)
				order[sort](textarea, page);
			printf("    textarea:\n");
			rectanglelist_printyaml(stdout,
				"      - ", "        ", textarea);
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
			rectangle_draw(cr, boundingbox, TRUE, FALSE, FALSE);
		else
			rectanglelist_draw(cr, textarea,
				FALSE, FALSE, numbers, FALSE);
		if (fits)
			rectangle_draw(cr, &moved, TRUE, TRUE, FALSE);
		cairo_destroy(cr);
		cairo_surface_show_page(surface);

		rectanglelist_free(textarea);
		poppler_rectangle_free(boundingbox);

		g_object_unref(page);
	}

	cairo_surface_destroy(surface);

	return EXIT_SUCCESS;
}


/*
 * fit a pdf in A4 (or some other paper size)
 *
 * TODO: bounding box should include images, not only text (pdfrect)
 * TODO: clip before copying page onto the cairo surface
 */

#include <stdlib.h>
#include <stdio.h>

#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>

#include "pdfrects.h"

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	char *infile, *uri, *outfile = NULL;
	gboolean usage = FALSE, opterror = FALSE;
	gboolean landscape = FALSE, ratio = TRUE, individual = FALSE;
	gboolean wholepage = FALSE, emptypages = FALSE;
	gboolean givendest = FALSE, givenmargin = FALSE;
	gboolean orig = FALSE, frame = FALSE, drawbb = FALSE, debug = FALSE;
	gdouble w, h;
	char *paper = NULL;
	PopplerRectangle *pagesize = NULL;
	PopplerRectangle pagedest, outdest, dest, test;
	gdouble defaultmargin = 40.0;
	gdouble marginx1, marginy1, marginx2, marginy2;

	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	PopplerRectangle *boundingbox, *pageboundingbox;
	PopplerRectangle psize = {0.0, 0.0, 0.0, 0.0};

	cairo_surface_t *surface;
	cairo_t *cr;

				/* arguments */

	while ((opt = getopt(argc, argv, "hiewfskbrldm:p:g:o:")) != -1)
		switch(opt) {
		case 'l':
			landscape = TRUE;
			break;
		case 'r':
			ratio = FALSE;
			break;
		case 'i':
			individual = TRUE;
			break;
		case 'e':
			emptypages = TRUE;
			break;
		case 'p':
			paper = optarg;
			if (2 == sscanf(optarg, "%lg,%lg", &w, &h)) {
				pagesize = poppler_rectangle_new();
				pagesize->x1 = 0.0;
				pagesize->y1 = 0.0;
				pagesize->x2 = w;
				pagesize->y2 = h;
			}
			break;
		case 'm':
			givenmargin = TRUE;
			if (4 == sscanf(optarg, "%lg,%lg,%lg,%lg",
				&marginx1, &marginy1, &marginx2, &marginy2))
				break;
			marginx1 = atof(optarg);
			marginy1 = atof(optarg);
			marginx2 = atof(optarg);
			marginy2 = atof(optarg);
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'w':
			wholepage = TRUE;
			defaultmargin = 0.0;
			break;
		case 'g':
			if (4 != sscanf(optarg, "%lg,%lg,%lg,%lg",
					&outdest.x1, &outdest.y1,
					&outdest.x2, &outdest.y2)) {
				printf("cannot parse box: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			givendest = TRUE;
			break;
		case 'k':
			paper = "ebook";
			pagesize = poppler_rectangle_new();
			pagesize->x1 = 0.0;
			pagesize->y1 = 0.0;
			pagesize->x2 = 200;
			pagesize->y2 = 250;
			defaultmargin = 5.0;
			break;
		case 'b':
			drawbb = TRUE;
			break;
		case 'f':
			frame = TRUE;
			break;
		case 's':
			orig = TRUE;
			break;
		case 'd':
			debug = TRUE;
			break;
		case 'h':
			usage = TRUE;
			break;
		default:
			usage = TRUE;
			opterror = TRUE;
		}

	if (argc - 1 < optind) {
		printf("input file name missing\n");
		usage = TRUE;
		opterror = TRUE;
	}

				/* usage */

	if (usage) {
		printf("pdffit fits a pdf into an A4 page\n");
		printf("usage:\n");
		printf("\tpdffit [options] file.pdf\n");
		printf("\t\t-l\t\tlandscape\n");
		printf("\t\t-i\t\tscale each page individually\n");
		printf("\t\t-e\t\tskip empty pages\n");
		printf("\t\t-r\t\tdo not maintain aspect ratio\n");
		printf("\t\t-p paper\tpaper size (a4, letter, 500,500...)\n");
		printf("\t\t-w\t\tresize the whole page, without margins\n");
		printf("\t\t-m margin\tminimal distance from border of page ");
		printf(          "to text\n");
		printf("\t\t-g box\t\tdestination box\n");
		printf("\t\t-k\t\tadapt to ebook viewing\n");
		printf("\t\t-f\t\tdraw the border of the destination page\n");
		printf("\t\t-s\t\tdraw the border of the original page\n");
		printf("\t\t-d\t\tdraw a square in a corner ");
		printf(          "to check margins\n");
		printf("\t\t-b\t\tdraw the bounding box of each page\n");
		printf("\t\t-h\t\tthis help\n");
		exit(opterror ? EXIT_FAILURE : EXIT_SUCCESS);
	}

				/* rectangles */

	if (paper == NULL)
		paper = defaultpapersize();
	if (paper == NULL)
		paper = "a4";

	if (pagesize == NULL)
		pagesize = get_papersize(paper);
	if (pagesize == NULL) {
		printf("no such paper size: %s\n", paper);
		exit(EXIT_FAILURE);
	}

	pagedest.x1 = landscape ? pagesize->y1 : pagesize->x1;
	pagedest.y1 = landscape ? pagesize->x1 : pagesize->y1;
	pagedest.x2 = landscape ? pagesize->y2 : pagesize->x2;
	pagedest.y2 = landscape ? pagesize->x2 : pagesize->y2;

	if (! givendest)
		outdest = pagedest;

	if (! givenmargin) {
		marginx1 = defaultmargin;
		marginy1 = defaultmargin;
		marginx2 = defaultmargin;
		marginy2 = defaultmargin;
	}

	dest.x1 = outdest.x1 + marginx1;
	dest.y1 = outdest.y1 + marginy1;
	dest.x2 = outdest.x2 - marginx2;
	dest.y2 = outdest.y2 - marginy2;

	test.x1 = pagedest.x1;
	test.y1 = pagedest.y1;
	test.x2 = dest.x1;
	test.y2 = dest.y1;

				/* file names */

	infile = argv[optind];
	uri = filenametouri(infile);
	if (! uri)
		exit(EXIT_FAILURE);
	if (outfile == NULL)
		outfile = pdfaddsuffix(infile, paper);
	printf("%s -> %s\n", infile, outfile);

				/* input file */

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	if (doc == NULL) {
		printf("error opening pdf file\n");
		exit(EXIT_FAILURE);
	}

	npages = poppler_document_get_n_pages(doc);
	if (npages < 1) {
		printf("no page in document\n");
		exit(EXIT_FAILURE);
	}

				/* destination surface */

	surface = cairo_pdf_surface_create(outfile, pagedest.x2, pagedest.y2);

				/* bounding box of all pages */

	if (! individual && ! wholepage)
		boundingbox = rectanglelist_boundingbox_document(doc);

				/* copy each page to destination */

	for (n = 0; n < npages; n++) {
		printf("page %-5d  ", n + 1);
		page = poppler_document_get_page(doc, n);
		poppler_page_get_size(page, &psize.x2, &psize.y2);
		pageboundingbox = rectanglelist_boundingbox(page);
		if (pageboundingbox == NULL && emptypages) {
			printf("\n");
			continue;
		}

		if (individual && ! wholepage)
			boundingbox = pageboundingbox;

		rectangle_print(stdout, wholepage ? &psize : boundingbox);
		printf(" -> ");
		rectangle_print(stdout, &dest);
		printf("\n");

		cr = cairo_create(surface);
		rectangle_map_to_cairo(cr, &dest,
			wholepage ? &psize : boundingbox,
			FALSE, FALSE,
			ratio,
			individual && n == npages - 1);
		poppler_page_render_for_printing(page, cr);

		if (drawbb)
			rectangle_draw(cr, boundingbox, TRUE, FALSE, FALSE);
		if (orig)
			rectangle_draw(cr, &psize, TRUE, FALSE, FALSE);
		cairo_identity_matrix(cr);
		if (frame)
			rectangle_draw(cr, &outdest, TRUE, FALSE, FALSE);
		if (debug)
			rectangle_draw(cr, &test, TRUE, TRUE, FALSE);

		cairo_destroy(cr);
		cairo_surface_show_page(surface);

		if (individual && ! wholepage)
			poppler_rectangle_free(boundingbox);
	}

	if (! individual && ! wholepage)
		poppler_rectangle_free(boundingbox);

	cairo_surface_destroy(surface);

	return EXIT_SUCCESS;
}


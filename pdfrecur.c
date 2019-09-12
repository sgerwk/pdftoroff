/*
 * pdfrecur.c
 *
 * locate or remove the recurring blocks of text in a PDF document
 *
 * aimed at removing page numbers, headers and footers
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
	char *infile, *outfile;
	gdouble recurheight = -1, distance = -1;
	gboolean draw = FALSE, clip = TRUE, noout = FALSE, usemain = FALSE;

	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	gdouble width, height;
	RectangleList *textarea, *flist;
	PopplerRectangle *maintext;

	cairo_surface_t *surface;
	cairo_t *cr;

				/* arguments */

	while ((opt = getopt(argc, argv, "s:t:mcdnh")) != -1)
		switch(opt) {
		case 's':
			height = atof(optarg);
			break;
		case 't':
			distance = atof(optarg);
			break;
		case 'm':
			usemain = TRUE;
			break;
		case 'c':
			clip = FALSE;
			break;
		case 'd':
			draw = TRUE;
			break;
		case 'n':
			noout = TRUE;
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
		printf("\tpdfrecur ");
		printf("[-s height] [-t distance] [-d] [-m] [-c] [-n] ");
		printf("[-h] file.pdf\n");
		printf("\t\t-s height\tmaximal height of recurring text\n");
		printf("\t\t-t distance\ttext-to-text distance\n");
		printf("\t\t-d\t\tdraw a box around removed rectangles\n");
		printf("\t\t-m\t\tuse the main text block in the page\n");
		printf("\t\t-c\t\tdo not remove recurring rectangles\n");
		printf("\t\t-n\t\tonly print recurring rectangles\n");
		printf("\t\t-h\t\tthis help\n");
		exit(EXIT_FAILURE);
	}
	infile = filenametouri(argv[optind]);
	if (! infile)
		exit(EXIT_FAILURE);
	outfile = pdfaddsuffix(argv[optind], "norecur");
	debugfrequent = 0x02 | 0x04;

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

				/* find the recurring text blocks */

	flist = rectanglevector_frequent(doc, recurheight, distance);
	if (usemain) {
		maintext = rectanglevector_main(doc, flist,
			recurheight, distance);
		printf("maintext:\n");
		rectangle_printyaml(stdout, "  - ", "    ", maintext);
	}
	if (noout)
		return 0;

				/* copy to destination */

	surface = cairo_pdf_surface_create(outfile, 1, 1);

	printf("infile: %s\n", argv[optind]);
	printf("outfile: %s\n", outfile);
	printf("pages: \n");

	for (n = 0; n < npages; n++) {
		printf("  - page: %d\n", n);
		page = poppler_document_get_page(doc, n);
		poppler_page_get_size(page, &width, &height);
		cairo_pdf_surface_set_size(surface, width, height);

		cr = cairo_create(surface);

		textarea = rectanglelist_textarea_distance(page, distance);
		cairo_save(cr);
		if (clip) {
			if (usemain) {
				rectangle_cairo(cr, maintext, 0);
				cairo_clip(cr);
			}
			else rectanglelist_clip_containing(cr, page,
				textarea, flist);
		}
		poppler_page_render_for_printing(page, cr);
		cairo_restore(cr);
		if (draw) {
			rectanglelist_draw(cr, flist,
				FALSE, FALSE, FALSE, FALSE);
			if (usemain)
				rectangle_draw(cr, maintext,
					FALSE, TRUE, FALSE);
		}

		cairo_destroy(cr);
		cairo_surface_show_page(surface);

		g_object_unref(page);
	}

	cairo_surface_destroy(surface);

	return EXIT_SUCCESS;
}


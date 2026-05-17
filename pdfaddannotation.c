/*
 * pdfaddannotation.c
 *
 * add a text annotation
 */

#include <stdlib.h>
#include <stdio.h>

#include <poppler.h>

#include "pdfname.h"

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	gboolean usage = FALSE;
	char *infile, *outfile = NULL, *saveuri;
	int pageno;
	char *text;
	char *search = NULL;

	PopplerDocument *doc;
	PopplerPage *page;
	double width, height;
	GList *list;
	PopplerAnnotMapping *mapping;

	PopplerAnnot *annot;
	PopplerAnnotText *ta;
	PopplerRectangle pos = {10, 10, 20, 20}, area;

				/* arguments */

	while ((opt = getopt(argc, argv, "o:h")) != -1)
		switch(opt) {
		case 'o':
			outfile = optarg;
			break;
		case 'h':
		default:
			usage = TRUE;
		}

	if (! usage && argc - 1 < optind) {
		printf("input file name missing\n");
		usage = TRUE;
	} else if (! usage && argc - 2 < optind) {
		printf("page number missing\n");
		usage = TRUE;
	} else if (! usage && argc - 3 < optind) {
		printf("annotation text missing\n");
		usage = TRUE;
	}
	if (usage) {
		printf("usage:\n");
		printf("\tpdfaddannotation [-o outfile.pdf] ");
		printf("[-h] file.pdf page text [x y|search]\n");
		printf("\t\t-h\t\tthis help\n");
		printf("\t\t-o outfile.pdf\toutput file\n");
		printf("\t\tfile.pdf\tinput file\n");
		printf("\t\tpage\t\tplace annotation in this page\n");
		printf("\t\tx y\t\tplace annotation at these coordinates\n");
		printf("\t\tsearch\t\tplace annotation where this text is\n");
		exit(EXIT_FAILURE);
	}
	infile = filenametouri(argv[optind]);
	if (! infile)
		exit(EXIT_FAILURE);
	pageno = atoi(argv[optind + 1]) - 1;
	text = argv[optind + 2];
	if (optind + 4 < argc) {
		pos.x1 = atof(argv[optind + 3]);
		pos.y1 = atof(argv[optind + 4]);
		pos.x2 = pos.x1 + 10;
		pos.y2 = pos.y1 + 10;
	}
	else if (optind + 3 < argc)
		search = argv[optind + 3];

				/* open file */

	doc = poppler_document_new_from_file(infile, NULL, NULL);
	if (doc == NULL) {
		printf("error opening pdf file\n");
		exit(EXIT_FAILURE);
	}

				/* add annotation */

	printf("infile: %s\n", argv[optind]);

	page = poppler_document_get_page(doc, pageno);
	list = search ? poppler_page_find_text(page, search) : NULL;
	if (list)
		area = *((PopplerRectangle *) list->data);
	else {
		poppler_page_get_size(page, &width, &height);
		area.x1 = pos.x1;
		area.y1 = height - pos.y2;
		area.x2 = pos.x2;
		area.y2 = height - pos.y1;
	}

	annot = poppler_annot_text_new(doc, &area);
	ta = (PopplerAnnotText *) annot;
	poppler_annot_set_contents(annot, text);
	poppler_annot_text_set_icon(ta, POPPLER_ANNOT_TEXT_ICON_NOTE);
	poppler_page_add_annot(page, annot);
	g_object_unref(annot);

	list = poppler_page_get_annot_mapping(page);
	mapping = list->data;
	ta = (PopplerAnnotText *) mapping->annot;
	pos = mapping->area;
	printf("%d ", poppler_page_get_index(page) + 1);
	printf("%s ", poppler_annot_get_contents(mapping->annot));
	printf("[%.f,%.f-%.f,%.f] ", pos.x1, pos.y1, pos.x2, pos.y2);
	printf("%s", poppler_annot_text_get_icon(ta));
	printf("\n");
	poppler_page_free_annot_mapping(list);

	g_object_unref(page);

	if (outfile == NULL)
		outfile = pdfaddsuffix(argv[optind], "-annot");
	printf("outfile: %s\n", outfile);
	saveuri = filenametouri(outfile);
	if (! poppler_document_save(doc, saveuri, NULL))
		printf("error saving file %s\n", outfile);

	return EXIT_SUCCESS;
}


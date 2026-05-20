/*
 * pdfannotation.c
 *
 * view or add text annotations
 *
 * position is either coordinate or text to search in the page
 *	- view annotation closest to the search text
 *	- add annotation at the search text
 */

#include <stdlib.h>
#include <stdio.h>

#include <poppler.h>

#include "pdfname.h"

//define ABS(x) ((x) > 0 ? (x) : -(x))
#define DIST(a, b) (ABS((a).x1 - (b).x1) + ABS((a).y1 - (b).y1))
#define INVERT(y) (height - (y))
#define REVERSE(y) { (y) = height - (y); }

int verbose = FALSE;
#define dprintf if (verbose) printf

/*
 * print an annotation
 */
void printtextannotation(PopplerPage *page, PopplerAnnotMapping *mapping) {
	PopplerAnnotText *ta;
	PopplerRectangle area;

	ta = (PopplerAnnotText *) mapping->annot;
	area = mapping->area;
	printf("%d ", poppler_page_get_index(page) + 1);
	printf("[%.f,%.f-%.f,%.f] ", area.x1, area.y1, area.x2, area.y2);
	printf("%s ", poppler_annot_text_get_icon(ta));
	printf("%s", poppler_annot_get_contents(mapping->annot));
	printf("\n");
}

/*
 * add a text annotation
 */
void addtextannotation(PopplerDocument *doc, PopplerPage *page,
                       gchar *text, PopplerRectangle *area) {
	PopplerAnnot *annot;
	PopplerAnnotText *ta;

	annot = poppler_annot_text_new(doc, area);
	ta = (PopplerAnnotText *) annot;
	poppler_annot_set_contents(annot, text);
	poppler_annot_text_set_icon(ta, POPPLER_ANNOT_TEXT_ICON_NOTE);
	poppler_page_add_annot(page, annot);
	g_object_unref(annot);
}

/*
 * save pdf file
 */
int savepdf(PopplerDocument *doc, char *infile, char *outfile) {
	int res;
	char *saveuri;

	if (outfile == NULL)
		outfile = pdfaddsuffix(infile, "-annot");
	dprintf("outfile: %s\n", outfile);

	saveuri = filenametouri(outfile);
	res = poppler_document_save(doc, saveuri, NULL);
	if (! res)
		printf("error saving file %s\n", outfile);
	return res;
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	gboolean usage = FALSE;
	char *infile, *outfile = NULL;
	gboolean remove = FALSE;
	int pageno;
	char *text = NULL;
	char *search = NULL;

	PopplerDocument *doc;
	PopplerPage *page;
	double width, height;
	GList *list, *elem;
	PopplerAnnotMapping *mapping, *closest;

	PopplerAnnot *annot;
	PopplerAnnotText *ta;
	PopplerRectangle pos = {10, 10, 20, 20}, area;
	double distance, min;

				/* arguments */

	while ((opt = getopt(argc, argv, "o:a:rvh")) != -1)
		switch(opt) {
		case 'o':
			outfile = optarg;
			break;
		case 'a':
			text = optarg;
			break;
		case 'r':
			remove = TRUE;
			break;
		case 'v':
			verbose = TRUE;
			break;
		case 'h':
		default:
			usage = TRUE;
		}

	if (! usage && argc - 1 < optind) {
		printf("input file name missing\n");
		usage = TRUE;
	}
	else if (! usage && argc - 2 < optind) {
		printf("page number missing\n");
		usage = TRUE;
	}
	if (usage) {
		printf("usage:\n");
		printf("\tpdfannotation [-o outfile.pdf] [-a text] [-r]\n");
		printf("\t              [-v] [-h] ");
		printf("file.pdf page [x y|search]\n");
		printf("\t\t-h\t\tthis help\n");
		printf("\t\t-o outfile.pdf\toutput file\n");
		printf("\t\t-a text\t\tadd text annotation\n");
		printf("\t\t-r\t\tremove or change annotation\n");
		printf("\t\tfile.pdf\tinput file\n");
		printf("\t\tpage\t\tpage number\n");
		printf("\t\tx y\t\tcoordinates\n");
		printf("\t\tsearch\t\tposition at text or closest to it\n");
		exit(EXIT_FAILURE);
	}
	infile = filenametouri(argv[optind + 0]);
	if (! infile)
		exit(EXIT_FAILURE);
	pageno = atoi(argv[optind + 1]) - 1;
	if (optind + 3 < argc) {
		pos.x1 = atof(argv[optind + 2]);
		pos.y1 = atof(argv[optind + 3]);
		pos.x2 = pos.x1 + 10;
		pos.y2 = pos.y1 + 10;
		search = "";
	}
	else if (optind + 2 < argc)
		search = argv[optind + 2];

				/* open file */

	doc = poppler_document_new_from_file(infile, NULL, NULL);
	if (doc == NULL) {
		printf("error opening pdf file\n");
		exit(EXIT_FAILURE);
	}

				/* page and position */

	dprintf("infile: %s\n", argv[optind]);
	page = poppler_document_get_page(doc, pageno);
	poppler_page_get_size(page, &width, &height);
	list = search ? poppler_page_find_text(page, search) : NULL;
	if (list)
		area = *((PopplerRectangle *) list->data);
	else {
		area.x1 = pos.x1;
		area.y1 = height - pos.y2;
		area.x2 = pos.x2;
		area.y2 = height - pos.y1;
	}

				/* add annotation */

	if (text && ! remove) {
		dprintf("position: %.f,%.f\n", area.x1, INVERT(area.y1));
		annot = poppler_annot_text_new(doc, &area);
		ta = (PopplerAnnotText *) annot;
		poppler_annot_set_contents(annot, text);
		poppler_annot_text_set_icon(ta, POPPLER_ANNOT_TEXT_ICON_NOTE);
		poppler_page_add_annot(page, annot);
		g_object_unref(annot);

		g_object_unref(page);
		savepdf(doc, argv[optind], outfile);
		return EXIT_SUCCESS;
	}

				/* view or remove annotations */

	list = poppler_page_get_annot_mapping(page);
	if (search == NULL)
		for (elem = list; elem; elem = elem->next) {
			mapping = (PopplerAnnotMapping *) elem->data;
			ta = (PopplerAnnotText *) mapping->annot;
			area = mapping->area;
			printtextannotation(page, mapping);
		}
	else {
		min = 1000000;
		pos = area;
		REVERSE(pos.y1);
		dprintf("closest to %.f,%.f\n", pos.x1, pos.y1);
		for (elem = list; elem; elem = elem->next) {
			mapping = (PopplerAnnotMapping *) elem->data;
			ta = (PopplerAnnotText *) mapping->annot;
			area = mapping->area;
			distance = DIST(pos, area);
			if (distance < min) {
				min = distance;
				closest = mapping;
			}
		}
		printtextannotation(page, closest);
		if (remove) {
			annot = closest->annot;
			if (! text) {
				dprintf("remove annotation\n");
				poppler_page_remove_annot(page, annot);
			}
			else {
				dprintf("change annotation\n");
				poppler_annot_set_contents(annot, text);
			}
			savepdf(doc, argv[optind], outfile);
		}
	}
	poppler_page_free_annot_mapping(list);
	return EXIT_SUCCESS;
}


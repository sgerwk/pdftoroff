/*
 * pdftoroff.c
 *
 * convert a pdf into various text formats, trying to undo page, column and
 * paragraph formatting while retaining italic and bold face
 *
 * the output format can be:
 *	-r	roff
 *	-w	html
 *	-p	plain TeX
 *	-f	text with \[fontname] for font changes and \\ for backslashes
 *	-t	text
 *	-s fmt	arbitrary struct format as fmt
 *
 * the format is a comma-separate list of strings
 * for example, html can also be generated by:

./pdftoroff -s '
<p>,</p>
,,,,,,<i>,</i>,<b>,</b>,true,\,.,&lt;,&gt;,&amp;' file.pdf

 *
 * other options:
 *	-m met	method for converting: 0-3
 *	-o ord	method for sorting: 0-2
 *	-d dis	minimal distance between blocks of text in the page
 *	-i n-m	page range
 *
 *
 * todo: see man page, section BUGS
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poppler.h>
#include "pdftext.h"

/*
 * main
 */
int main(int argc, char *argv[]) {
	gboolean usage, opterr;
	int method = 1, order = 1;
	int first = 1, last = 0;
	struct measure measure = {8, 25, 80, 30, 40, 6, 20, 15, '-'};
	struct format *format;
	PopplerRectangle *zone = NULL;

				/* arguments */

	format = &format_roff;
	usage = FALSE;
	opterr = FALSE;
	while (argc > 1 && argv[1][0] == '-') {
		switch(argv[1][1]) {
		case 'r':
			format = &format_roff;
			break;
		case 'w':
			format = &format_html;
			break;
		case 'p':
			format = &format_tex;
			break;
		case 'f':
			format = &format_textfont;
			break;
		case 't':
			format = &format_text;
			break;
		case 's':
			if (argc - 1 < 2) {
				printf("-s requires a format\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			format = parseformat(argv[2]);
			if (format == NULL) {
				printf("invalid format: %s\n", argv[2]);
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			argc--;
			argv++;
			break;
		case 'm':
			method = atoi(argv[2]);
			if (argc - 1 < 2 || method < 0 || method > 4) {
				printf("-m requires a method (0-4)\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			argc--;
			argv++;
			break;
		case 'd':
			if (argc - 1 < 2) {
				printf("-d requires a distance\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			measure.blockdistance = atoi(argv[2]);
			argc--;
			argv++;
			break;
		case 'o':
			order = atoi(argv[2]);
			if (argc - 1 < 2 || order < 0 || order > 2) {
				printf("-o requires an algorithm (0-2)\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			argc--;
			argv++;
			break;
		case 'i':
			if (argc - 1 < 2 ||
			    sscanf(argv[2], "%d:%d", &first, &last) != 2) {
				printf("-i requires a page range (n:m)\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			argc--;
			argv++;
			break;
		case 'b':
			if (argc - 1 < 2) {
				printf("-b requires a box\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			zone = rectangle_parse(argv[2]);
			if (zone == NULL) {
				printf("error parsing box\n");
				usage = TRUE;
				opterr = TRUE;
				break;
			}
			argc--;
			argv++;
			break;
		case 'v':
			debugpar = TRUE;
			break;
		case 'h':
			usage = TRUE;
			opterr = FALSE;
			break;
		default:
			printf("option not recognized: %s\n", argv[1]);
			usage = TRUE;
			opterr = TRUE;
			break;
		}
		argc--;
		argv++;
	}
	if (argc - 1 < 1 || usage) {
		printf("pdftoroff converts pdf to various text formats\n");
		printf("usage:\n\tpdftoroff [-r|-w|-p|-f|-t|-s fmt]");
		printf(" [-m method [-d dist] [-o order]]\n");
		printf("\t          [-i range] [-b box] [-v] file.pdf\n");
		printf("\t\t-r\t\tconvert to roff (default)\n");
		printf("\t\t-w\t\tconvert to html\n");
		printf("\t\t-p\t\tconvert to plain TeX\n");
		printf("\t\t-f\t\tconvert to text with font changes\n");
		printf("\t\t-t\t\tconvert to text\n");
		printf("\t\t-s fmt\t\toutput format strings\n");
		printf("\t\t-m method\tconversion method (0-3)\n");
		printf("\t\t-d distance\tminimal distance between ");
		printf("blocks of text\n");
		printf("\t\t-o order\tblock sorting algorithm (0-2)\n");
		printf("\t\t-i range\tpages to convert (n:m)\n");
		printf("\t\t-b box\t\tonly convert characters in box\n");
		printf("\t\t-v\t\treason for line breaks\n");

		exit(opterr || ! usage ? EXIT_FAILURE : EXIT_SUCCESS);
	}

				/* show file */

	showfile(stdout, argv[1], first - 1, last - 1, zone,
		method, order, &measure, format);

	return EXIT_SUCCESS;
}


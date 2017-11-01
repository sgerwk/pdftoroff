/*
 * pdftext.c
 *
 * convert pdf to text or enriched text (roff, html)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poppler.h>
#include "pdfrects.h"
#include "pdftext.h"

/*
 * known output formats
 */
struct format format_roff = {
	".ti 1\n", "\n",
	"",
	"\\fR", "\\fI", "\\fB", "\\f[BI]",
	"", "", "", "",
	FALSE,
	"\\", "\\[char46]", "<", ">", "&"
};
struct format format_html = {
	"\n<p>", "</p>\n",
	"",
	"", "", "", "",
	"<i>", "</i>", "<b>", "</b>",
	TRUE,
	"\\", ".", "&lt;", "&gt;", "&amp;"
};
struct format format_tex = {
	"", "\n\n",
	"",
	"\\rm ", "\\it ", "\\bf ", "\\bf ", /* FIXME: bold+italic */
	"", "", "", "",
	FALSE,
	"\\backslash ", ".", "<", ">", "\\& "
};
struct format format_textfont = {
	"", "\n",
	"\\[%s]",
	"", "", "", "",
	"", "", "", "",
	FALSE,
	"\\\\", ".", "<", ">", "&"
};
struct format format_text = {
	"", "\n",
	"",
	"", "", "", "",
	"", "", "", "",
	FALSE,
	"\\", ".", "<", ">", "&"
};

/*
 * print reason for a paragraph break
 */
gboolean debugpar = FALSE;
void dnewpar(FILE *fd, char *why) {
	if (debugpar)
		fprintf(fd, why);
}

/*
 * start or end a font face
 *	start	TRUE to start the new face, FALSE to end the previous
 *	reset	TRUE to temporarily end or restore all active faces
 */
void face(FILE *fd, gboolean start, gboolean reset,
		gboolean *italic, gboolean *bold,
		PopplerTextAttributes *attr, struct format *format) {
	gboolean newitalic, newbold;

	if (reset && ! format->reset)
		return;

	newitalic = NULL != strstr(attr->font_name, "Italic");
	newbold = NULL != strstr(attr->font_name, "Bold");

				/* font name */

	if (start && ! reset && *format->fontname != '\0')
		fprintf(fd, format->fontname, attr->font_name);

				/* font start, no end except for resets */

	if (start) {
		if (! newitalic && ! newbold)
			fprintf(fd, format->plain);
		else if (newitalic && ! newbold)
			fprintf(fd, format->italic);
		else if (! newitalic && newbold)
			fprintf(fd, format->bold);
		if (newitalic && newbold)
			fprintf(fd, format->bolditalic);
	}
	if (! start && reset)
		fprintf(fd, format->plain);

				/* font start-end */

	if (! start) {
		if (*bold && newbold == reset)
			fprintf(fd, format->boldend);
		if (*italic && newitalic == reset)
			fprintf(fd, format->italicend);
	}
	else {
		if (*italic == reset && newitalic)
			fprintf(fd, format->italicbegin);
		if (*bold == reset && newbold)
			fprintf(fd, format->boldbegin);
	}

				/* update current font */

	if (start && ! reset) {
		*italic = newitalic;
		*bold = newbold;
	}
}

/*
 * show a single character
 */
void showcharacter(FILE *fd, char *cur, char *next, gboolean newpar,
		struct format *format) {
	if (*cur == '\\')
		fprintf(fd, format->backslash);
	else if (newpar && *cur == '.')
		fprintf(fd, format->firstdot);
	else if (*cur == '<')
		fprintf(fd, format->less);
	else if (*cur == '>')
		fprintf(fd, format->greater);
	else if (*cur == '&')
		fprintf(fd, format->and);
	else if (*cur != '-' || (*next && *next != '\n'))
		fwrite(cur, 1, next - cur, fd);
}

#ifdef _PDFRECTS_H

/*
 * show the characters in a box
 */
void showbox(FILE *fd, PopplerPage *page, PopplerRectangle *zone,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char **prev) {
	char *text, *cur, *next;
	int count;
	gdouble left, y;

	GList *attrlist, *attrelem;
	RectangleList *textarea;
	int ti = -1;
	PopplerRectangle *tr;
	PopplerTextAttributes *attr;
	gboolean detectcolumn;
	gboolean startcolumn, newline, italic, bold, hyphen, newface;

	PopplerRectangle *rects, crect;
	guint nrects, r;

				/* page content */

	text = poppler_page_get_text(page);
	attrlist = poppler_page_get_text_attributes(page);
	if (! text || ! attrlist)
		return;		/* no text in page */
	if (! poppler_page_get_text_layout(page, &rects, &nrects))
		return;		/* no text in page */

				/* text area to print */

	switch (method) {
	case 0:
		tr = poppler_rectangle_new();
		poppler_page_get_crop_box(page, tr);
		textarea = rectanglelist_new(1);
		rectanglelist_add(textarea, tr);
		detectcolumn = TRUE;
		break;
	case 1:
		tr = rectanglelist_boundingbox(page);
		textarea = rectanglelist_new(1);
		rectanglelist_add(textarea, tr);
		detectcolumn = FALSE;
		break;
	case 2:
		textarea = rectanglelist_textarea_distance(page,
				measure->blockdistance);
		detectcolumn = FALSE;
		break;
	case 3:
		textarea = rectanglelist_new(1);
		rectanglelist_add(textarea, zone);
		detectcolumn = FALSE;
		break;
	default:
		fprintf(stderr, "no such conversion method: %d\n", method);
		exit(EXIT_FAILURE);
	}

				/* cycle over (utf-8) characters in page */

	startcolumn = TRUE;
	italic = FALSE;
	bold = FALSE;
	hyphen = FALSE;
	newface = TRUE;

	attrelem = attrlist;
	attr = (PopplerTextAttributes *) (attrelem->data);

	for (cur = text, count = 0; *cur; cur = next, count++) {
		crect = rects[count];
		next = g_utf8_next_char(cur);
		if (zone != NULL && ! rectangle_contain(zone, &crect))
			continue;

					/* text rectangle this char is in */

		if (ti != -1 && rectangle_contain(tr, &crect))
			newline = FALSE;
		else {
			ti = rectanglelist_contain(textarea, &crect);
			if (debugpar && method != 3)
				fprintf(fd, "\n=== BLOCK %d ===\n", ti);
			if (ti != -1)
				tr = &textarea->rect[ti];
			else if (*cur == ' ') {
				dnewpar(fd, "_SPACE_");
				ti = -1;
				tr = &crect;
			}
			else {
				fprintf(stderr, "\n");
				fprintf(stderr, "error: cannot find ");
				fprintf(stderr, "text rectangle\n");
				fprintf(stderr, "character: %c ", *cur);
				fprintf(stderr, "(%d)\n", *cur);
				fprintf(stderr, "rectangle:\n");
				rectangle_print(stderr, &crect);
				fprintf(stderr, "text area:\n");
				rectanglelist_print(stderr, textarea);
				exit(EXIT_FAILURE);
			}
			left = tr->x1;
			y = tr->y1 - measure->newline - 1;
			newline = TRUE;
		}

					/* explicit end of line */

		if (newline || *cur == '\n') {
			if (crect.x2 - left <
			    (tr->x2 - left) * measure->rightreturn / 100) {
				dnewpar(fd, "[1]");
				*newpar = TRUE;
			}
			else if (! hyphen)
				*prev = " ";
		}

					/* real character */

		if (*cur != '\n') {

					/* new column */

			if (detectcolumn &&
			    crect.x1 - left >
				(tr->x2 - tr->x1) * measure->newcolumnx / 100
				&&
			    y - crect.y1 >
				(tr->y2 - tr->y1) * measure->newcolumny / 100)
				startcolumn = TRUE;

			if (detectcolumn && startcolumn) {
				left = 10000;
				y = 10000;
				for (r = MAX(measure->headfooter, count);
				     r + measure->headfooter < nrects;
				     r++) {
					left = MIN(left, rects[r].x1);
					y = MIN(y, rects[r].y1);
				}
				if (left == 10000)
					y = 0; /* few chars, force newpar */
				y -= measure->newline + 1;
				startcolumn = FALSE;
			}

					/* y increase */

			if (crect.y1 - y > measure->newline) {
				if (crect.y1 - y > measure->newpar) {
					dnewpar(fd, "[2]");
					*newpar = TRUE;
				}
				y = crect.y1;
				if (crect.x1 - left >
						measure->indenttolerance) {
					dnewpar(fd, "[3]");
					*newpar = TRUE;
				}
			}

					/* new paragraph */

			if (*newpar) {
				face(fd, FALSE, TRUE,
					&italic, &bold, attr, format);
				if (! ! strcmp(*prev, "start"))
					fprintf(fd, format->parend);
				fprintf(fd, format->parstart);
				face(fd, TRUE, TRUE,
					&italic, &bold, attr, format);
			}
			else
				fprintf(fd, *prev);
			*prev = "";

					/* start a new font face */

			if (newface && *cur != ' ') {
				face(fd, TRUE, FALSE,
					&italic, &bold, attr, format);
				newface = FALSE;
			}

					/* print character */

			showcharacter(fd, cur, next, *newpar, format);

					/* status of hyphen and newpar */

			hyphen = *cur == '-';
			*newpar = FALSE;
		}

				/* end of text with current font; read next */

		if (count == attr->end_index -
			     (g_unichar_isspace(*next) ? 1 : 0)) {
			attrelem = g_list_next(attrelem);
			if (! attrelem) {
				face(fd, FALSE, TRUE,
					&italic, &bold, attr, format);
				break;
			}
			attr = (PopplerTextAttributes *) (attrelem->data);
			face(fd, FALSE, FALSE,
				&italic, &bold, attr, format);
			newface = TRUE;
		}
	}

			/* end of page is like a carriage return in text */

	if (crect.x2 - left < (tr->x2 - left) * measure->rightreturn / 100) {
		dnewpar(fd, "[4]");
		*newpar = TRUE;
	}
	else if (! hyphen)
		*prev = " ";

	poppler_page_free_text_attributes(attrlist);
	g_free(rects);
	free(text);
}

/*
 * show the characters in a page
 */
void showpage(FILE *fd, PopplerPage *page,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char **prev) {
	RectangleList *textarea;
	gint r;

	if (method != 3) {
		showbox(fd, page, NULL, method, measure, format, newpar, prev);
		return;
	}

	textarea =
		rectanglelist_textarea_distance(page, measure->blockdistance);
	rectanglelist_sort(textarea);
	for (r = 0; r < textarea->num; r++) {
		if (debugpar)
			fprintf(fd, "\n=== BLOCK %d ===\n", r);
		showbox(fd, page, &textarea->rect[r], 3,
			measure, format, newpar, prev);
	}
}

#else

/*
 * show a page, no-pdfrects version
 */
void showpage(FILE *fd, PopplerPage *page,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char **prev) {
	char *text, *cur, *next;
	int count;
	gdouble x = 0, y = 0;

	PopplerRectangle crop;
	gdouble width, height;
	GList *attrlist, *attrelem;
	PopplerTextAttributes *attr;
	gboolean startcolumn, italic, bold, hyphen, newface;

	PopplerRectangle *rects, crect;
	guint nrects, r;

				/* method */

	if (method != 0) {
		fprintf(stderr, "this version only supports method 0\n");
		exit(EXIT_FAILURE);
	}

				/* page content */

	text = poppler_page_get_text(page);
	attrlist = poppler_page_get_text_attributes(page);
	if (! text || ! attrlist)
		return;		/* no text in page */
	if (! poppler_page_get_text_layout(page, &rects, &nrects))
		return;		/* no text in page */
	if (0)
		poppler_page_get_size(page, &width, &height);
	else {
		poppler_page_get_crop_box(page, &crop);
		width = crop.x2 - crop.x1;
		height = crop.y2 - crop.y1;
	}

	attrelem = attrlist;
	attr = (PopplerTextAttributes *) (attrelem->data);

				/* cycle over (utf-8) characters in page */

	startcolumn = TRUE;
	italic = FALSE;
	bold = FALSE;
	hyphen = FALSE;
	newface = TRUE;

	for (cur = text, count = 0; *cur; cur = next, count++) {
		crect = rects[count];
		next = g_utf8_next_char(cur);

					/* carriage return in text */

		if (*cur == '\n') {
			if (crect.x2 < width * measure->rightreturn / 100) {
				dnewpar(fd, "[1]");
				*newpar = TRUE;
			}
			else if (! hyphen)
				*prev = " ";
		}
		else {

					/* new column */

			if (crect.x1 - x > width * measure->newcolumnx / 100 &&
			    y - crect.y1 > height * measure->newcolumny / 100)
				startcolumn = TRUE;

			if (startcolumn) {
				x = 10000;
				y = 10000;
				for (r = MAX(measure->headfooter, count);
				     r + measure->headfooter < nrects;
				     r++) {
					x = MIN(x, rects[r].x1);
					y = MIN(y, rects[r].y1);
				}
				if (x == 10000)
					y = 0; /* few chars, force newpar */
				y -= measure->newline + 1;
				startcolumn = FALSE;
			}

					/* new paragraph */

			if (crect.y1 - y > measure->newline) {
				if (crect.y1 - y > measure->newpar) {
					dnewpar(fd, "[2]");
					*newpar = TRUE;
				}
				y = crect.y1;
				if (x < crect.x1 - measure->indenttolerance) {
					dnewpar(fd, "[3]");
					*newpar = TRUE;
				}
			}

			if (*newpar) {
				face(fd, FALSE, TRUE,
					&italic, &bold, attr, format);
				if (! ! strcmp(*prev, "start"))
					fprintf(fd, format->parend);
				fprintf(fd, format->parstart);
				face(fd, TRUE, TRUE,
					&italic, &bold, attr, format);
			}
			else
				fprintf(fd, *prev);
			*prev = "";

					/* start a new font face */

			if (newface && *cur != ' ') {
				face(fd, TRUE, FALSE,
					&italic, &bold, attr, format);
				newface = FALSE;
			}

					/* print character */

			showcharacter(fd, cur, next, *newpar, format);

					/* status of hyphen and newpar */

			hyphen = *cur == '-';
			*newpar = FALSE;
		}

				/* end of text with current font; read next */

		if (count == attr->end_index -
			     (g_unichar_isspace(*next) ? 1 : 0)) {
			attrelem = g_list_next(attrelem);
			if (! attrelem) {
				face(fd, FALSE, TRUE,
					&italic, &bold, attr, format);
				break;
			}
			attr = (PopplerTextAttributes *) (attrelem->data);
			face(fd, FALSE, FALSE, &italic, &bold, attr, format);
			newface = TRUE;
		}
	}

			/* end of page is like a carriage return in text */

	if (crect.x2 < width * measure->rightreturn / 100) {
		dnewpar(fd, "[4]");
		*newpar = TRUE;
	}
	else if (! hyphen)
		*prev = " ";

	poppler_page_free_text_attributes(attrlist);
	g_free(rects);
	free(text);
}

/*
 * from file name to uri
 */
char *filenametouri(char *filename) {
	char *dir, *sep, *uri;

	if (filename[0] == '/') {
		dir = "";
		sep = "";
	}
	else {
		dir = malloc(4096);
		if (dir == NULL) {
			fprintf(stderr,
				"failed to allocate memory for directory\n");
			exit(EXIT_FAILURE);
		}
		if (getcwd(dir, 4096) == NULL) {
			fprintf(stderr,
				"error in obtaining the current directory\n");
			exit(EXIT_FAILURE);
		}
		sep = "/";
	}

	uri = malloc(strlen("file:") + strlen(dir) +
		strlen(sep) + strlen(filename) + 1);
	if (uri == NULL) {
		fprintf(stderr, "failed to allocate memory for file name\n");
		exit(EXIT_FAILURE);
	}
	strcpy(uri, "file:");
	strcat(uri, dir);
	strcat(uri, sep);
	strcat(uri, filename);

	return uri;
}

#endif

/*
 * end a document
 */
void enddocument(FILE *fd, struct format *format, char *prev) {
	if (! ! strcmp(prev, "start"))
		fprintf(fd, format->parend);
}

/*
 * show some pages of a pdf document
 */
void showdocumentpart(FILE *fd, PopplerDocument *doc, int first, int last,
		int method, struct measure *measure, struct format *format) {
	PopplerPage *page;
	gboolean newpar = TRUE;
	char *prev = "start";
	int npage;

	for (npage = first; npage <= last; npage++) {
		page = poppler_document_get_page(doc, npage);
		showpage(fd, page, method, measure, format, &newpar, &prev);
	}
	enddocument(fd, format, prev);
}

/*
 * show a pdf document
 */
void showdocument(FILE *fd, PopplerDocument *doc, int method,
		struct measure *measure, struct format *format) {
	showdocumentpart(fd, doc, 0, poppler_document_get_n_pages(doc) - 1,
		method, measure, format);
}

/*
 * show a pdf file
 */
void showfile(FILE *fd, char *filename, int method,
		struct measure *measure, struct format *format) {
	char *uri;
	PopplerDocument *doc;

	uri = filenametouri(filename);

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	if (doc == NULL) {
		printf("error opening file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	showdocument(fd, doc, method, measure, format);
	free(uri);
}

/*
 * parse a string into a struct format
 */
struct format *parseformat(char *s) {
	struct format *f;
	char *c, *t;

	c = strdup(s);
	f = malloc(sizeof(struct format));
	memset(f, 0, sizeof(struct format));

	if (! (f->parstart = strsep(&c, ",")))
		return NULL;
	if (! (f->parend = strsep(&c, ",")))
		return NULL;
	if (! (f->fontname = strsep(&c, ",")))
		return NULL;
	if (! (f->plain = strsep(&c, ",")))
		return NULL;
	if (! (f->italic = strsep(&c, ",")))
		return NULL;
	if (! (f->bold = strsep(&c, ",")))
		return NULL;
	if (! (f->bolditalic = strsep(&c, ",")))
		return NULL;
	if (! (f->italicbegin = strsep(&c, ",")))
		return NULL;
	if (! (f->italicend = strsep(&c, ",")))
		return NULL;
	if (! (f->boldbegin = strsep(&c, ",")))
		return NULL;
	if (! (f->boldend = strsep(&c, ",")))
		return NULL;
	if (! (t = strsep(&c, ",")))
		return NULL;
	f->reset = ! strcmp(t, "true");
	if (! (f->backslash = strsep(&c, ",")))
		return NULL;
	if (! (f->firstdot = strsep(&c, ",")))
		return NULL;
	if (! (f->less = strsep(&c, ",")))
		return NULL;
	if (! (f->greater = strsep(&c, ",")))
		return NULL;
	if (! (f->and = strsep(&c, ",")))
		return NULL;

	return f;
}


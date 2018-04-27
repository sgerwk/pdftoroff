/*
 * pdftext.c
 *
 * convert pdf to text or rich text (roff, html, tex)
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
		fputs(why, fd);
}
void delement(FILE *fd, char *what, int num) {
	if (debugpar)
		fprintf(fd, what, num);
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
			fputs(format->plain, fd);
		else if (newitalic && ! newbold)
			fputs(format->italic, fd);
		else if (! newitalic && newbold)
			fputs(format->bold, fd);
		if (newitalic && newbold)
			fputs(format->bolditalic, fd);
	}
	if (! start && reset)
		fputs(format->plain, fd);

				/* font start-end */

	if (! start) {
		if (*bold && newbold == reset)
			fputs(format->boldend, fd);
		if (*italic && newitalic == reset)
			fputs(format->italicend, fd);
	}
	else {
		if (*italic == reset && newitalic)
			fputs(format->italicbegin, fd);
		if (*bold == reset && newbold)
			fputs(format->boldbegin, fd);
	}

				/* update current font */

	if (start && ! reset) {
		*italic = newitalic;
		*bold = newbold;
	}
}

/*
 * show a single character
 *	rest	character not written (hyphen at end of line) or NONE
 */
void showcharacter(FILE *fd, char *cur, char *next, char *rest,
		gboolean newpar, struct format *format) {
	*rest = NONE;
	if (*cur == '\\')
		fputs(format->backslash, fd);
	else if (newpar && *cur == '.')
		fputs(format->firstdot, fd);
	else if (*cur == '<')
		fputs(format->less, fd);
	else if (*cur == '>')
		fputs(format->greater, fd);
	else if (*cur == '&')
		fputs(format->and, fd);
	else if (*cur == '-' && (*next == '\0' || *next == '\n'))
		*rest = '-';
	else
		fwrite(cur, 1, next - cur, fd);
}

/*
 * check if a line is short
 */
gboolean isshortline(PopplerRectangle crect, gdouble left, gdouble right,
		struct measure *measure) {
	return crect.x2 - left < (right - left) * measure->rightreturn / 100;
}

/*
 * check start of new column
 */
gboolean newcolumn(gdouble y, PopplerRectangle crect,
		gdouble left, PopplerRectangle *tr,
		struct measure *measure) {
	return
		crect.x1 - left > (tr->x2 - tr->x1) * measure->newcolumnx / 100
		&&
		y - crect.y1 > (tr->y2 - tr->y1) * measure->newcolumny / 100;
}

/*
 * box-not-found error
 */
void boxnotfound(char *cur, PopplerRectangle *crect, RectangleList *textarea) {
	fprintf(stderr, "error: cannot find text rectangle\n");
	fprintf(stderr, "character: %c (%d)\n", *cur, *cur);
	fprintf(stderr, "rectangle:\n");
	rectangle_print(stderr, crect);
	fprintf(stderr, "\n");
	fprintf(stderr, "text area:\n");
	rectanglelist_print(stderr, textarea);
	exit(EXIT_FAILURE);
}

/*
 * show the characters in a box
 *	newpar	paragraph in previous box/page is already known to be over
 *	prev	unprinted character from previous box, possibly NONE or START
 */
void showbox(FILE *fd, PopplerPage *page, PopplerRectangle *zone,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev) {
	char *text, *cur, *next;
	int count;
	gdouble left, y;

	GList *attrlist, *attrelem;
	RectangleList *textarea;
	int ti = -1;
	PopplerRectangle *tr;
	PopplerTextAttributes *attr;
	gboolean detectcolumn;
	gboolean startcolumn, shortline, newline;
	gboolean italic, bold, newface;

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

	shortline = FALSE;
	startcolumn = TRUE;
	italic = FALSE;
	bold = FALSE;
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
			delement(fd, "[BLOCK %d]", ti);
			if (ti != -1)
				tr = &textarea->rect[ti];
			else if (*cur == ' ') {
				dnewpar(fd, "_SPACE_");
				ti = -1;
				tr = &crect;
			}
			else
				boxnotfound(cur, &crect, textarea);
			left = tr->x1;
			y = tr->y1 - measure->newline - 1;
			newline = TRUE;
		}

					/* explicit end of line */

		if (*cur == '\n' || newline) {
			if (shortline) {
				dnewpar(fd, "[1]");
				*newpar = TRUE;
			}
			else {
				dnewpar(fd, "[]");
				*prev = *prev == '-' || *prev == START ?
						NONE : ' ';
			}
		}

					/* real character */

		if (*cur != '\n') {

					/* new column */

			if (detectcolumn &&
			    newcolumn(y, crect, left, tr, measure))
				startcolumn = TRUE;

			if (detectcolumn && startcolumn) {
				dnewpar(fd, "[COLUMN]");
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
				if (crect.x1 - left > measure->indent) {
					dnewpar(fd, "[3]");
					*newpar = TRUE;
				}
			}

					/* new paragraph */

			if (*newpar) {
				face(fd, FALSE, TRUE,
					&italic, &bold, attr, format);
				if (*prev != START)
					fputs(format->parend, fd);
				fputs(format->parstart, fd);
				face(fd, TRUE, TRUE,
					&italic, &bold, attr, format);
			}
			else if (*prev > START)
				fprintf(fd, "%c", *prev);

					/* start a new font face */

			if (newface && *cur != ' ') {
				face(fd, TRUE, FALSE,
					&italic, &bold, attr, format);
				newface = FALSE;
			}

					/* print character */

			showcharacter(fd, cur, next, prev, *newpar, format);

					/* update status veriables */

			shortline = isshortline(crect, left, tr->x2, measure);
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

				/* shortline at the end of box or page */

	if (shortline) {
		dnewpar(fd, "[4]");
		*newpar = TRUE;
	}

	poppler_page_free_text_attributes(attrlist);
	g_free(rects);
	free(text);
}

/*
 * show the characters in a page
 */
void showpage(FILE *fd, PopplerPage *page,
		int method, int order,
		struct measure *measure, struct format *format,
		gboolean *newpar, char *prev) {
	RectangleList *textarea;
	gint r;
	void (*sort[])(RectangleList *, PopplerPage *) = {
		rectanglelist_quicksort,
		rectanglelist_twosort,
		rectanglelist_charsort
	};

	if (method != 3) {
		showbox(fd, page, NULL, method, measure, format, newpar, prev);
		return;
	}

	textarea =
		rectanglelist_textarea_distance(page, measure->blockdistance);
	sort[order](textarea, page);
	for (r = 0; r < textarea->num; r++) {
		delement(fd, "[=== BLOCK %d]", r);
		showbox(fd, page, &textarea->rect[r], 3, measure, format,
			newpar, prev);
	}
}

/*
 * start processing a document
 */
void startdocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev) {
	(void)fd;
	(void)method;
	(void)measure;
	(void)format;
	*newpar = FALSE;
	*prev = START;
}

/*
 * end a document
 */
void enddocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev) {
	(void)method;
	(void)measure;
	(void)newpar;
	if (*prev != START)
		fputs(format->parend, fd);
}

/*
 * show some pages of a pdf document
 */
void showdocumentpart(FILE *fd, PopplerDocument *doc, int first, int last,
		int method, int order,
		struct measure *measure, struct format *format) {
	gboolean newpar;
	char prev;
	int npage;
	PopplerPage *page;

	if (first < 0)
		first = poppler_document_get_n_pages(doc) + first;
	if (last < 0)
		last = poppler_document_get_n_pages(doc) + last;

	if (first < 0)
		first = 0;
	if (last >= poppler_document_get_n_pages(doc))
		last = poppler_document_get_n_pages(doc) - 1;

	startdocument(fd, method, measure, format, &newpar, &prev);
	for (npage = first; npage <= last; npage++) {
		page = poppler_document_get_page(doc, npage);
		delement(fd, "[PAGE %d]", npage);
		showpage(fd, page, method, order,
			measure, format, &newpar, &prev);
		g_object_unref(page);
	}
	enddocument(fd, method, measure, format, &newpar, &prev);
}

/*
 * show a pdf document
 */
void showdocument(FILE *fd, PopplerDocument *doc,
		int method, int order,
		struct measure *measure, struct format *format) {
	showdocumentpart(fd, doc, 0, -1, method, order, measure, format);
}

/*
 * show a pdf file
 */
void showfile(FILE *fd, char *filename, int first, int last,
		int method, int order,
		struct measure *measure, struct format *format) {
	char *uri;
	PopplerDocument *doc;

	uri = filenametouri(filename);

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	free(uri);
	if (doc == NULL) {
		printf("error opening file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	showdocumentpart(fd, doc, first, last, method, order, measure, format);
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
		goto parse_error;
	if (! (f->parend = strsep(&c, ",")))
		goto parse_error;
	if (! (f->fontname = strsep(&c, ",")))
		goto parse_error;
	if (! (f->plain = strsep(&c, ",")))
		goto parse_error;
	if (! (f->italic = strsep(&c, ",")))
		goto parse_error;
	if (! (f->bold = strsep(&c, ",")))
		goto parse_error;
	if (! (f->bolditalic = strsep(&c, ",")))
		goto parse_error;
	if (! (f->italicbegin = strsep(&c, ",")))
		goto parse_error;
	if (! (f->italicend = strsep(&c, ",")))
		goto parse_error;
	if (! (f->boldbegin = strsep(&c, ",")))
		goto parse_error;
	if (! (f->boldend = strsep(&c, ",")))
		goto parse_error;
	if (! (t = strsep(&c, ",")))
		goto parse_error;
	f->reset = ! strcmp(t, "true");
	if (! (f->backslash = strsep(&c, ",")))
		goto parse_error;
	if (! (f->firstdot = strsep(&c, ",")))
		goto parse_error;
	if (! (f->less = strsep(&c, ",")))
		goto parse_error;
	if (! (f->greater = strsep(&c, ",")))
		goto parse_error;
	if (! (f->and = strsep(&c, ",")))
		goto parse_error;

	return f;

parse_error:
	free(f);
	return NULL;
}


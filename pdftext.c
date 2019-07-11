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
 * data for processing the characters
 */
struct scandata {
	gboolean newpar;	// last paragraph in previous box/page is over
	char prev;		// unprinted char, possibly NONE or START
	gboolean italic;	// current font is italic
	gboolean bold;		// current font is bold
	gboolean newface;	// font changed
};

/*
 * start processing a page (no end needed)
 */
void startpage(struct scandata *scanpage) {
	scanpage->italic = FALSE;
	scanpage->bold = FALSE;
	scanpage->newface = TRUE;
}

/*
 * show the characters of a page contained in a box
 *	zone		only characters in this box are shown
 * 			NULL = all characters in page
 *	textarea	the blocks of text in the page
 *			may also be the whole page or its bounding box
 *	text, attrlist, rects, nrects
 *			characters and their fonts and positions
 *	detectcolumns	whether to detect the start of a new column during the
 *			scan, by comparing the position of the current
 *			character with that of the current column and previous
 *			character; only used when textarea=page
 */
void showregion(FILE *fd, PopplerRectangle *zone, RectangleList *textarea,
		char *text, GList *attrlist,
		PopplerRectangle *rects, guint nrects,
		struct measure *measure, struct format *format,
		struct scandata *scandata, gboolean detectcolumn) {
	char *cur, *next;
	int count;
	gdouble left, y;

	GList *attrelem;
	int ti = -1;
	PopplerRectangle *tr;
	PopplerTextAttributes *attr;
	gboolean startcolumn, shortline, newline;

	PopplerRectangle crect;
	guint r;

				/* cycle over (utf-8) characters in page */

	shortline = FALSE;
	startcolumn = TRUE;

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
				dnewpar(fd, "[S]");
				scandata->newpar = TRUE;
			}
			else {
				if (scandata->prev == '-')
					dnewpar(fd, "[-]");
				else
					dnewpar(fd, "[]");
				scandata->prev =
					scandata->prev == '-' ||
					scandata->prev == START ?
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
					dnewpar(fd, "[V]");
					scandata->newpar = TRUE;
				}
				y = crect.y1;
				if (crect.x1 - left > measure->indent) {
					dnewpar(fd, "[I]");
					scandata->newpar = TRUE;
				}
			}

					/* new paragraph */

			if (scandata->newpar) {
				face(fd, FALSE, TRUE,
					&scandata->italic, &scandata->bold,
					attr, format);
				if (scandata->prev != START)
					fputs(format->parend, fd);
				fputs(format->parstart, fd);
				face(fd, TRUE, TRUE,
					&scandata->italic, &scandata->bold,
					attr, format);
			}
			else if (scandata->prev > START)
				fprintf(fd, "%c", scandata->prev);

					/* start a new font face */

			if (scandata->newface && *cur != ' ') {
				face(fd, TRUE, FALSE,
					&scandata->italic, &scandata->bold,
					attr, format);
				scandata->newface = FALSE;
			}

					/* print character */

			showcharacter(fd, cur, next,
				&scandata->prev, scandata->newpar, format);

					/* update status variables */

			shortline = isshortline(crect, left, tr->x2, measure);
			scandata->newpar = FALSE;
		}

				/* end of text with current font; read next */

		if (count == attr->end_index -
			     (g_unichar_isspace(*next) ? 1 : 0)) {
			attrelem = g_list_next(attrelem);
			if (! attrelem) {
				face(fd, FALSE, TRUE,
					&scandata->italic, &scandata->bold,
					attr, format);
				break;
			}
			attr = (PopplerTextAttributes *) (attrelem->data);
			face(fd, FALSE, FALSE,
				&scandata->italic, &scandata->bold,
				attr, format);
			scandata->newface = TRUE;
		}
	}

				/* shortline at end */

	if (shortline) {
		dnewpar(fd, "[E]");
		scandata->newpar = TRUE;
	}
}

/*
 * show the characters in a page
 */
void showpage(FILE *fd, PopplerPage *page, PopplerRectangle *zone,
		int method, int order,
		struct measure *measure, struct format *format,
		struct scandata *scandata) {
	char *text;
	GList *attrlist;
	PopplerRectangle *rects, *tr, *region;
	guint nrects;
	RectangleList *textarea;
	gint r;
	void (*sort[])(RectangleList *, PopplerPage *) = {
		rectanglelist_quicksort,
		rectanglelist_twosort,
		rectanglelist_charsort
	};

				/* initalize output font */

	startpage(scandata);

				/* get page content */

	text = poppler_page_get_text(page);
	attrlist = poppler_page_get_text_attributes(page);
	if (! text || ! attrlist)
		return;		/* no text in page */
	if (! poppler_page_get_text_layout(page, &rects, &nrects))
		return;		/* no text in page */

				/* analyze text */

	switch (method) {
	case 0:
		tr = poppler_rectangle_new();
		poppler_page_get_crop_box(page, tr);
		textarea = rectanglelist_new(1);
		rectanglelist_add(textarea, tr);
		showregion(fd, zone, textarea, text, attrlist, rects, nrects,
			measure, format, scandata, TRUE);
		poppler_rectangle_free(tr);
		break;
	case 1:
		tr = rectanglelist_boundingbox(page);
		textarea = rectanglelist_new(1);
		rectanglelist_add(textarea, tr);
		showregion(fd, zone, textarea, text, attrlist, rects, nrects,
			measure, format, scandata, FALSE);
		poppler_rectangle_free(tr);
		break;
	case 2:
		textarea = rectanglelist_textarea_distance(page,
				measure->blockdistance);
		showregion(fd, zone, textarea, text, attrlist, rects, nrects,
			measure, format, scandata, FALSE);
		break;
	case 3:
		textarea = rectanglelist_textarea_distance(page,
				measure->blockdistance);
		sort[order](textarea, page);
		region = poppler_rectangle_new();
		for (r = 0; r < textarea->num; r++) {
			delement(fd, "[=== BLOCK %d]", r);
			if (zone == NULL) {
				showregion(fd, &textarea->rect[r], textarea,
					text, attrlist, rects, nrects,
					measure, format, scandata, FALSE);
				continue;
			}
			if (! rectangle_overlap(zone, &textarea->rect[r]))
				continue;
			rectangle_intersect(region, zone, &textarea->rect[r]);
			showregion(fd, region, textarea,
				text, attrlist, rects, nrects,
				measure, format, scandata, FALSE);
		}
		poppler_rectangle_free(region);
		break;
	case 4:
		measure->rightreturn = -1;
		textarea = rectanglelist_rows(page);
		region = poppler_rectangle_new();
		for (r = 0; r < textarea->num; r++) {
			delement(fd, "[=== BLOCK %d]", r);
			if (zone == NULL) {
				showregion(fd, &textarea->rect[r], textarea,
					text, attrlist, rects, nrects,
					measure, format, scandata, FALSE);
				measure->indent = -1;
				continue;
			}
			if (! rectangle_overlap(zone, &textarea->rect[r]))
				continue;
			rectangle_intersect(region, zone, &textarea->rect[r]);
			showregion(fd, region, textarea,
				text, attrlist, rects, nrects,
				measure, format, scandata, FALSE);
			measure->indent = -1;
		}
		poppler_rectangle_free(region);
		break;
	default:
		fprintf(stderr, "no such conversion method: %d\n", method);
		exit(EXIT_FAILURE);
	}

	poppler_page_free_text_attributes(attrlist);
	g_free(rects);
	free(text);
}

/*
 * start processing a document
 */
void startdocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		struct scandata *scandata) {
	(void)fd;
	(void)method;
	(void)measure;
	(void)format;
	scandata->newpar = FALSE;
	scandata->prev = START;
}

/*
 * end a document
 */
void enddocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		struct scandata *scandata) {
	(void)method;
	(void)measure;
	if (scandata->prev != START)
		fputs(format->parend, fd);
}

/*
 * show some pages of a pdf document
 */
void showdocumentpart(FILE *fd, PopplerDocument *doc, int first, int last,
		PopplerRectangle *zone,
		int method, int order,
		struct measure *measure, struct format *format) {
	struct scandata scandata;
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

	startdocument(fd, method, measure, format, &scandata);
	for (npage = first; npage <= last; npage++) {
		page = poppler_document_get_page(doc, npage);
		delement(fd, "[PAGE %d]", npage);
		showpage(fd, page, zone,
			method, order, measure, format, &scandata);
		g_object_unref(page);
	}
	enddocument(fd, method, measure, format, &scandata);
}

/*
 * show a pdf document
 */
void showdocument(FILE *fd, PopplerDocument *doc, PopplerRectangle *zone,
		int method, int order,
		struct measure *measure, struct format *format) {
	showdocumentpart(fd, doc, 0, -1, zone, method, order, measure, format);
}

/*
 * show a pdf file
 */
void showfile(FILE *fd, char *filename, int first, int last,
		PopplerRectangle *zone,
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

	showdocumentpart(fd, doc, first, last, zone,
		method, order, measure, format);
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


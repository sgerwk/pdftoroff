/*
 * pdftext.h
 *
 * convert pdf to text or rich text (roff, html, tex)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poppler.h>
#include "pdfrects.h"

/*
 * parameters for the input (D=delta, %=percentage of page)
 */
struct measure {
	int newline;		/* more Dy than this is a newline */
	int newpar;		/* more Dy than this is a new paragraph */
	int rightreturn;	/* line end before this x% is a new paragraph */
	int newcolumnx;		/* more than this Dx% is new column (and) */
	int newcolumny;		/* more than this -Dy% is new column (and) */
	int indent;		/* more than this at start of line is indent */
	int headfooter;		/* ignore x,y of chars at begin/end of page */
	int blockdistance;	/* distance between blocks of text */
};

/*
 * output strings
 */
struct format {
	char *parstart;		/* paragraph start */
	char *parend;		/* paragraph end */

	char *fontname;		/* format for printing font names, or NULL */

	char *plain;		/* set font face */
	char *italic;
	char *bold;
	char *bolditalic;

	char *italicbegin;	/* begin/end font face */
	char *italicend;
	char *boldbegin;
	char *boldend;

	gboolean reset;		/* reset and restart face at par breaks */

	char *backslash;
	char *firstdot;		/* substitute this for dot at start of line */
	char *less;
	char *greater;
	char *and;
};

/*
 * known output formats
 */
extern struct format format_roff;
extern struct format format_html;
extern struct format format_tex;
extern struct format format_textfont;
extern struct format format_text;

/* print reason for a paragraph break */
extern gboolean debugpar;

/* previous character, keep START at the end */
#define NONE '\0'
#define START '\1'

/* in the functions for extracting text from boxes or pages, define newpar
 * and prev; then pass &newpar, &prev unchanged from one call to the next */

/* start processing a document */
void startdocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev);

/* show the characters in a box in a page */
void showbox(FILE *fd, PopplerPage *page, PopplerRectangle *zone,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev);

/* show the characters in a page */
void showpage(FILE *fd, PopplerPage *page,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev);

/* end processing a document */
void enddocument(FILE *fd,
		int method, struct measure *measure, struct format *format,
		gboolean *newpar, char *prev);

/* show some pages of a pdf document */
void showdocumentpart(FILE *fd, PopplerDocument *doc, int first, int last,
		int method, struct measure *measure, struct format *format);

/* show a pdf document */
void showdocument(FILE *fd, PopplerDocument *doc,
		int method, struct measure *measure, struct format *format);

/* show a pdf file */
void showfile(FILE *fd, char *filename,
		int method, struct measure *measure, struct format *format);

/* parse a string into a struct format */
struct format *parseformat(char *s);


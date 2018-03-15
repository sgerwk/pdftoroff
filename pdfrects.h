/*
 * pdfrects.h
 *
 * functions on poppler rectangles:
 *
 * - functions on individual rectangles (containment, join, etc)
 * - functions for rectangle lists representing the union of the areas
 * - functions for blocks of text in a page
 *
 * the functions that find the blocks of text first nullify the rectangles of
 * white space by turning them into 0-width rectangles; the resulting rectangle
 * list may not contain them
 */

#ifdef _PDFRECTS_H
#else
#define _PDFRECTS_H

/*
 * functions on individual rectangles
 */

/* print a rectangle */
void rectangle_print(FILE *, PopplerRectangle *);
void rectangle_printyaml(FILE *, char *first, char *indent, PopplerRectangle *);

/* check whether the first rectangle contains the second */
gboolean rectangle_contain(PopplerRectangle *, PopplerRectangle *);

/* check if rectangles overlap */
gboolean rectangle_overlap(PopplerRectangle *, PopplerRectangle *);

/* check if rectangles touch (meet or overlap) */
gboolean rectangle_touch(PopplerRectangle *, PopplerRectangle *);

/* copy a rectangle onto another */
void rectangle_copy(PopplerRectangle *dest, PopplerRectangle *orig);

/* shift a rectangle */
void rectangle_shift(PopplerRectangle *, gdouble x, gdouble y);

/* join rectangles: the first becomes the smallest rectangle containing both */
void rectangle_join(PopplerRectangle *, PopplerRectangle *);

/* compare the position of two rectangles */
int rectangle_compare(PopplerRectangle *, PopplerRectangle *);

/*
 * functions on lists of rectangles
 */

#define MAXRECT 50000
typedef struct {
	/* public */
	PopplerRectangle *rect;
	gint num;

	/* private */
	gint max;
} RectangleList;

/* allocate a list with maximum number of elements, currently none */
RectangleList *rectanglelist_new(int);

/* thighten a rectangle list by deallocating the unused entries */
void rectanglelist_tighten(RectangleList *);

/* free a rectangle list */
void rectanglelist_free(RectangleList *);

/* print a rectangle list */
void rectanglelist_print(FILE *, RectangleList *);
void rectanglelist_printyaml(FILE *, char *first, char *indent,
	RectangleList *);

/* remove a rectangle from a list */
void rectanglelist_delete(RectangleList *, gint);

/* add a rectangle to a list, if not redundant */
gboolean rectanglelist_add(RectangleList *, PopplerRectangle *);

/* smallest rectangle enclosing all in a rectangle list */
PopplerRectangle *rectanglelist_joinall(RectangleList *);

/* index of first rectangle in list in a relation to another rectangle */
gint rectanglelist_contain(RectangleList *, PopplerRectangle *);
gint rectanglelist_touch(RectangleList *, PopplerRectangle *);
gint rectanglelist_overlap(RectangleList *, PopplerRectangle *);

/* sort a rectangle list by position */
void rectanglelist_sort(RectangleList *);

/* position a rectangle in a page partially filled by others */
gboolean rectanglelist_place(PopplerRectangle *page,
		RectangleList *rl, PopplerRectangle *r,
		PopplerRectangle *moved);

/*
 * functions on text-enclosing rectangles
 * rectangles of white spaces ' ' are made 0-width
 */

/* debug */
extern int debugtextrectangles;

/* the rectangles of the single characters in the page */
RectangleList *rectanglelist_characters(PopplerPage *);

/* area of text in a page */
RectangleList *rectanglelist_textarea(PopplerPage *);

/* area of text in a page, with minimal distance considered a white space */
RectangleList *rectanglelist_textarea_distance(PopplerPage *, gdouble);

/* bounding box of the page (NULL if no text is in the page) */
PopplerRectangle *rectanglelist_boundingbox(PopplerPage *);

/* overall bounding box of the whole document (NULL if no text) */
PopplerRectangle *rectanglelist_boundingbox_document(PopplerDocument *doc);

/*
 * drawing-related functions
 */

/* draw a rectangle, possibly filled or enclosing */
void rectangle_draw(cairo_t *, PopplerRectangle *,
	gboolean randomcolor, gboolean fill, gboolean enclosing);

/* draw a rectangle list, possibly numbering each */
void rectanglelist_draw(cairo_t *, RectangleList *,
	gboolean fill, gboolean enclosing, gboolean num);

/* map a poppler rectangle into a cairo surface */
void rectangle_map_to_cairo(cairo_t *cr,
		PopplerRectangle *dst, PopplerRectangle *src,
		gboolean horizontal, gboolean ratio, gboolean topalign);

/* from name to paper size (pointer to statically allocated structure) */
PopplerRectangle *get_papersize(char *name);

/* default paper size, from /etc/papersize */
char *defaultpapersize();

/*
 * helper filename functions
 */

char *filenametouri(char *);
char *pdfaddsuffix(char *infile, char *suffix);

#endif


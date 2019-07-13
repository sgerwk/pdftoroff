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

/* parse a rectangle */
PopplerRectangle *rectangle_parse(char *s);

/* normalize a rectangle: x1 <= x2 and y1 <= y2 */
void rectangle_normalize(PopplerRectangle *);

/* width and height of a rectangle */
double rectangle_width(PopplerRectangle *);
double rectangle_height(PopplerRectangle *);

/* check whether the first rectangle contains the second */
gboolean rectangle_contain(PopplerRectangle *, PopplerRectangle *);

/* check if rectangles overlap */
gboolean rectangle_overlap(PopplerRectangle *, PopplerRectangle *);

/* check if rectangles touch (meet or overlap) */
gboolean rectangle_htouch(PopplerRectangle *a, PopplerRectangle *b);
gboolean rectangle_vtouch(PopplerRectangle *a, PopplerRectangle *b);
gboolean rectangle_touch(PopplerRectangle *, PopplerRectangle *);

/* horizontal and vertical distance between rectangles */
gdouble rectangle_hdistance(PopplerRectangle *a, PopplerRectangle *b);
gdouble rectangle_vdistance(PopplerRectangle *a, PopplerRectangle *b);

/* copy a rectangle onto another */
void rectangle_copy(PopplerRectangle *dest, PopplerRectangle *orig);

/* shift or expand a rectangle */
void rectangle_shift(PopplerRectangle *, gdouble x, gdouble y);
void rectangle_expand(PopplerRectangle *, gdouble dx, gdouble dy);

/* make the first rectangle the intersection of the other two */
void rectangle_intersect(PopplerRectangle *r,
		PopplerRectangle *a, PopplerRectangle *b);

/* join rectangles: the first becomes the smallest rectangle containing both */
void rectangle_join(PopplerRectangle *, PopplerRectangle *);

/* compare the position of two rectangles */
int rectangle_hcompare(PopplerRectangle *a, PopplerRectangle *b);
int rectangle_vcompare(PopplerRectangle *a, PopplerRectangle *b);
int rectangle_compare(PopplerRectangle *, PopplerRectangle *);

/* a rectangle as large as the page */
void rectangle_page(PopplerPage *page, PopplerRectangle *rect);

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

/*
 * minimal size for both dimensions of a rectangle and for each
 */
typedef struct {
	gdouble both;
	gdouble each;
} RectangleBound;

/* allocate a list with maximum number of elements, currently none */
RectangleList *rectanglelist_new(int);

/* make a copy of a rectangle list */
RectangleList *rectanglelist_copy(RectangleList *src);

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

/* append a rectangle to a list */
void rectanglelist_append(RectangleList *rl, PopplerRectangle *rect);

/* add a rectangle to a list, if not redundant */
gboolean rectanglelist_add(RectangleList *, PopplerRectangle *);

/* smallest rectangle enclosing all in a rectangle list */
PopplerRectangle *rectanglelist_joinall(RectangleList *);

/* horizontal or vertical extents of a rectangle list */
RectangleList *rectanglelist_hextents(RectangleList *);
RectangleList *rectanglelist_vextents(RectangleList *);

/* total width and height of a rectangle list */
double rectanglelist_sumwidth(RectangleList *rl);
double rectanglelist_sumheight(RectangleList *rl);

/* average width and height of a rectangle list */
double rectanglelist_averagewidth(RectangleList *rl);
double rectanglelist_averageheight(RectangleList *rl);

/* index of first rectangle in list in a relation to another rectangle */
gint rectanglelist_contain(RectangleList *, PopplerRectangle *);
gint rectanglelist_touch(RectangleList *, PopplerRectangle *);
gint rectanglelist_overlap(RectangleList *, PopplerRectangle *);

/* sort a rectangle list by position */
void rectanglelist_quicksort(RectangleList *, PopplerPage *);
void rectanglelist_twosort(RectangleList *, PopplerPage *);
void rectanglelist_charsort(RectangleList *, PopplerPage *);

/* position a rectangle in a page partially filled by others */
gboolean rectanglelist_place(PopplerRectangle *page,
		RectangleList *rl, PopplerRectangle *r,
		PopplerRectangle *moved);

/* subtract a rectangle list from another: orig -= sub */
gboolean rectanglelist_subtract(RectangleList **orig, RectangleList *sub,
		PopplerRectangle *cont, RectangleBound *b);

/* subtract a rectangle list from a single rectangle: res = r - rl */
RectangleList *rectanglelist_subtract1(PopplerRectangle *r, RectangleList *rl,
		PopplerRectangle *cont, RectangleBound *b);

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

/* list of squares of a grid that are painted in a page */
RectangleList *rectanglelist_painted(PopplerPage *page, int distance);

/* area of painted squares in a page, with minimal distance of white space */
RectangleList *rectanglelist_paintedarea_distance(PopplerPage *, gdouble);

/* bounding box of a page, based on painted squares */
PopplerRectangle *rectanglelist_boundingbox_painted(PopplerPage *page, int d);

/* list of rows in a page */
RectangleList *rectanglelist_rows(PopplerPage *page);

/*
 * drawing-related functions
 */

/* draw a rectangle, possibly filled or enclosing */
void rectangle_draw(cairo_t *, PopplerRectangle *,
	gboolean randomcolor, gboolean fill, gboolean enclosing);

/* draw a rectangle list, possibly numbering each */
void rectanglelist_draw(cairo_t *, RectangleList *,
	gboolean fill, gboolean enclosing, gboolean num, gboolean inside);

/* apply the current transformation to a rectangle */
void rectangle_transform(cairo_t *cr, PopplerRectangle *r);

/* map a poppler rectangle into a cairo surface */
void rectangle_map_to_cairo(cairo_t *cr,
		PopplerRectangle *dst, PopplerRectangle *src,
		gboolean horizontal, gboolean vertical,
		gboolean ratio, gboolean topalign, gboolean leftalign);

/*
 * helper functions
 */

/* from name to paper size (pointer to statically allocated structure) */
PopplerRectangle *get_papersize(char *name);

/* default paper size, from /etc/papersize */
char *defaultpapersize();

/* from file name to uri */
char *filenametouri(char *);

/* turn file.pdf into file-suffix.pdf */
char *pdfaddsuffix(char *infile, char *suffix);

#endif


/*
 * pdfrects.c
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
 *
 * RectangleList *rectanglelist_textarea(PopplerPage *);
 *	a list of rectangles that do not touch or overlap and cover all text in
 *	the page
 *
 * RectangleList *rectanglelist_textarea_distance(PopplerPage *, gdouble *);
 *	the second argument is the minimal distance to consider a white space;
 *	lower values lead to finer coverings of the used area
 *
 * PopplerRectangle *rectanglelist_boundingbox(PopplerPage *);
 * 	the overall bounding box of the page: the smallest rectangle that
 * 	covers all text in the page
 *
 * PopplerRectangle *rectanglelist_boundingbox_document(PopplerDocument *);
 * 	overall bounding box of the whole document: smallest rectangle that
 * 	contains all text in all pages
 */

/*
 * the algorithm for finding blocks of text:
 *
 * 1. C = list containing a rectangle for each character in the page
 *    (obtained from poppler)
 *
 * 2. C = join consecutive rectangles of C if they touch or overlap
 *    (this step is only for efficiency)
 *
 * 3. W = list comprising only a rectangle as large as the whole page
 *    for each rectangle R in C:
 *       - subtract R from each rectangle in W
 *         (each subtraction may generate up to four rectangles)
 *    now W is the white area of the page
 *
 * 4. B = list comprising only a rectangle as large as the whole page
 *    for each rectangle R in W:
 *       - subtract R from each rectangle in B
 *    now B covers the used area of the page
 *
 * 5. for each pair of rectangles in B:
 *       - if they touch or overlap, join them
 *    repeat until nothing changes
 *
 * 6. return B
 *
 * variable debugtextrectangle is for the intermediate lists of rectangles:
 *   - if its value x is greater than zero, the algorithm is cut short after
 *     step x and the current list of rectangles is returned; this allows for
 *     visualizing the intermediate results
 *   - also, the number of rectangles after each step is printed
 *   - if its value is -1, the number of rectangles is printed every time it
 *     changes during a subtraction
 */

/*
 * rectangle sorting
 *
 * rectangles can be sorted in two ways:
 * 1. by their positions
 * 2. by the order their characters occur in the document
 *
 * rectanglelist_charsort() is of the second kind: the block that contains the
 * first character in the page is the first block; the second block is that
 * containing the first character not in the first block, and so on
 *
 * the order based on the position of the rectangles can be obtained by
 * progressively refining an order between rectangles:
 *
 * a. if two rectangles overlap horizontally, order them by their y1
 * b. transitively close the resulting ordering
 * c. if two rectangles are still incomparable, order them by x1
 *
 * rectanglelist_quicksort() skips step b. for the sake of code simplicity
 *
 * rectanglelist_twosort() implements the ordering in two steps: first, a
 * variant of insertionsort that keeps into account the lack of transitive
 * closure sorts the elements that overlap horizontally based on their y1;
 * second, a variant of bubblesort sorts the elements according to their x1
 * while not swapping elements that overlap horizontally
 */

/*
 * todo:
 *	images
 *	fix rectangle sorting by position
 *	rectangle sorting by characters
 *	page size (from cmdline or from original file)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>

#include "pdfrects.h"

/*
 * return the intermediate list of rectangles after the step of this number in
 * the algorithm (see above); if not zero, print number of rectangles at each
 * step; if -1, print the number of rectangles every time it changes
 */
int debugtextrectangles = 0;

/*
 * print a rectangle
 */
void rectangle_print(FILE *fd, PopplerRectangle *r) {
	if (r == NULL)
		fprintf(fd, "[]");
	else
		fprintf(fd, "[%g,%g-%g,%g]", r->x1, r->y1, r->x2, r->y2);
}
void rectangle_printyaml(FILE *fd, char *first, char *indent,
		PopplerRectangle *r) {
	if (r == NULL) {
		printf("%sNULL\n", indent);
		return;
	}
	fprintf(fd, "%sx1: %g\n", first, r->x1);
	fprintf(fd, "%sy1: %g\n", indent, r->y1);
	fprintf(fd, "%sx2: %g\n", indent, r->x2);
	fprintf(fd, "%sy2: %g\n", indent, r->y2);
}

/*
 * parse a rectangle
 */
PopplerRectangle *rectangle_parse(char *s) {
	gdouble x1, y1, x2, y2;
	PopplerRectangle *res;

	if (sscanf(s, "[%lg,%lg-%lg,%lg]", &x1, &y1, &x2, &y2) != 4)
		return NULL;

	res = poppler_rectangle_new();
	res->x1 = x1;
	res->y1 = y1;
	res->x2 = x2;
	res->y2 = y2;
	return res;
}

/*
 * normalize a rectangle: x1 <= x2 and y1 <= y2
 */
void rectangle_normalize(PopplerRectangle *rect) {
	gdouble temp;
	if (rect->x1 > rect->x2) {
		temp = rect->x1;
		rect->x1 = rect->x2;
		rect->x2 = temp;
	}
	if (rect->y1 > rect->y2) {
		temp = rect->y1;
		rect->y1 = rect->y2;
		rect->y2 = temp;
	}
}

/* width and height of a rectangle */
double rectangle_width(PopplerRectangle *r) {
	return r->x2 - r->x1;
}
double rectangle_height(PopplerRectangle *r) {
	return r->y2 - r->y1;
}

/*
 * a rectangle as large as the page
 */
void rectangle_page(PopplerPage *page, PopplerRectangle *rect) {
	rect->x1 = 0;
	rect->y1 = 0;
	poppler_page_get_size(page, &rect->x2, &rect->y2);
}

/*
 * check if rectangle satisfies the bounds: both dimensions and at least one
 */
gboolean rectangle_bound(PopplerRectangle *r, RectangleBound *b) {
	return	r->x2 - r->x1 > b->both && r->y2 - r->y1 > b->both &&
		(r->x2 - r->x1 > b->each || r->y2 - r->y1 > b->each);
}

/*
 * check if rectangle a contains rectangle b
 */
gboolean rectangle_contain(PopplerRectangle *a, PopplerRectangle *b) {
	return	a->x1 <= b->x1 && a->y1 <= b->y1 &&
		a->x2 >= b->x2 && a->y2 >= b->y2;
}

/*
 * check if rectangles overlap
 */
gboolean rectangle_overlap(PopplerRectangle *a, PopplerRectangle *b) {
	return ! (a->x2 <= b->x1 || a->x1 >= b->x2 ||
		  a->y2 <= b->y1 || a->y1 >= b->y2);
}

/*
 * check if rectangles touch (meet or overlap) horizontally
 */
gboolean rectangle_htouch(PopplerRectangle *a, PopplerRectangle *b) {
	return ! (a->x2 < b->x1 || a->x1 > b->x2);
}

/*
 * check if rectangles touch (meet or overlap) vertically
 */
gboolean rectangle_vtouch(PopplerRectangle *a, PopplerRectangle *b) {
	return ! (a->y2 < b->y1 || a->y1 > b->y2);
}

/*
 * check if rectangles touch (meet or overlap)
 */
gboolean rectangle_touch(PopplerRectangle *a, PopplerRectangle *b) {
	return rectangle_htouch(a, b) && rectangle_vtouch(a, b);
}

/*
 * check whether a rectangle satisfies bounds and containment
 */
gboolean rectangle_boundcontain(PopplerRectangle *r,
                                PopplerRectangle *contained,
                                RectangleBound *bounds) {
	if (bounds != NULL && ! rectangle_bound(r, bounds))
		return FALSE;
	if (contained != NULL && ! rectangle_contain(r, contained))
		return FALSE;
	return TRUE;
}

/*
 * copy a rectangle onto another
 */
void rectangle_copy(PopplerRectangle *dest, PopplerRectangle *orig) {
	memcpy(dest, orig, sizeof(PopplerRectangle));
}

/*
 * swap two rectangles
 */
void rectangle_swap(PopplerRectangle *a, PopplerRectangle *b) {
	PopplerRectangle temp;
	temp = *a;
	*a = *b;
	*b = temp;
}

/*
 * shift a rectangle
 */
void rectangle_shift(PopplerRectangle *r, gdouble x, gdouble y) {
	r->x1 += x;
	r->y1 += y;
	r->x2 += x;
	r->y2 += y;
}

/*
 * make the first rectangle the intersection of the other two
 */
void rectangle_intersect(PopplerRectangle *r,
		PopplerRectangle *a, PopplerRectangle *b) {
	r->x1 = MAX(a->x1, b->x1);
	r->y1 = MAX(a->y1, b->y1);
	r->x2 = MIN(a->x2, b->x2);
	r->y2 = MIN(a->y2, b->y2);
}

/*
 * join rectangles: the first becomes the smallest rectangle containing both
 */
void rectangle_join(PopplerRectangle *a, PopplerRectangle *b) {
	if (b == NULL)
		return;
	a->x1 = MIN(a->x1, b->x1);
	a->y1 = MIN(a->y1, b->y1);
	a->x2 = MAX(a->x2, b->x2);
	a->y2 = MAX(a->y2, b->y2);
}

/*
 * compare the position of two rectangles
 */
int rectangle_hcompare(PopplerRectangle *a, PopplerRectangle *b) {
	return a->x1 < b->x1 ? -1 : a->x1 == b->x1 ? 0 : 1;
}
int rectangle_vcompare(PopplerRectangle *a, PopplerRectangle *b) {
	return a->y1 < b->y1 ? -1 : a->y1 == b->y1 ? 0 : 1;
}
int rectangle_compare(PopplerRectangle *a, PopplerRectangle *b) {
	return rectangle_htouch(a, b) ?
		rectangle_vcompare(a, b) :
		rectangle_hcompare(a, b);
}

/*
 * allocate a rectangle list with maximum n rectangles and currently none
 */
RectangleList *rectanglelist_new(int n) {
	RectangleList *res;

	res = malloc(sizeof(RectangleList));
	res->num = 0;
	res->max = n;
	res->rect = malloc(res->max * sizeof(PopplerRectangle));

	return res;
}

/*
 * make a copy of a rectangle list
 */
RectangleList *rectanglelist_copy(RectangleList *src) {
	RectangleList *dst;

	dst = rectanglelist_new(src->max);
	dst->num = src->num;
	memcpy(dst->rect, src->rect, dst->max * sizeof(PopplerRectangle));

	return dst;
}

/*
 * thighten a rectangle list by deallocating the unused entries
 */
void rectanglelist_tighten(RectangleList *r) {
	r->max = r->num;
	r->rect = realloc(r->rect, r->max * sizeof(PopplerRectangle));
}

/*
 * free a rectangle list
 */
void rectanglelist_free(RectangleList *rl) {
	if (rl == NULL)
		return;
	free(rl->rect);
	free(rl);
}

/*
 * print a rectangle list
 */
void rectanglelist_print(FILE *fd, RectangleList *rl) {
	gint r;

	for (r = 0; r < rl->num; r++) {
		rectangle_print(fd, &rl->rect[r]);
		fprintf(fd, "\n");
	}
}
void rectanglelist_printyaml(FILE *fd, char *first, char *indent,
		RectangleList *rl) {
	gint r;

	for (r = 0; r < rl->num; r++)
		rectangle_printyaml(fd, first, indent, &rl->rect[r]);
}

/*
 * remove a rectangle from a list
 */
void rectanglelist_delete(RectangleList *rl, gint n) {
	if (n >= rl->num)
		return;
	rectangle_copy(rl->rect + n, rl->rect + --rl->num);
}

/*
 * append a rectangle to a list
 */
void rectanglelist_append(RectangleList *rl, PopplerRectangle *rect) {
	if (rl->num >= rl->max)
		return;
	rectangle_copy(rl->rect + rl->num++, rect);
}

/*
 * add a rectangle to a list
 *
 * since a RectangleList represents the area that is the union of its
 * rectangles, when adding a new rectangle some simplifications can be done:
 *
 * - the request is ignored if the rectangle is contained in one in the list
 * - if the rectangle contains some rectangles in the list, it replaces them
 *
 * still, a rectangle list can be redundant: for example, a rectangle may be
 * contained in the union of other two
 */
gboolean rectanglelist_add(RectangleList *rl, PopplerRectangle *rect) {
	gint r;
	gboolean placed;

	if (rl->num >= rl->max)
		return FALSE;

	placed = FALSE;

	for (r = 0; r < rl->num; r++) {
		if (rectangle_contain(rl->rect + r, rect))
			return TRUE;

		if (rectangle_contain(rect, rl->rect + r)) {
			if (! placed) {
				rectangle_copy(rl->rect + r, rect);
				placed = TRUE;
			}
			else
				rectanglelist_delete(rl, r);
		}
	}

	if (! placed)
		rectanglelist_append(rl, rect);
	return TRUE;
}

/*
 * smallest rectangle enclosing all in a rectangle list
 */
PopplerRectangle *rectanglelist_joinall(RectangleList *rl) {
	PopplerRectangle *a;
	int r;

	if (rl == NULL || rl->num == 0)
		return NULL;

	a = poppler_rectangle_copy(&rl->rect[0]);
	for (r = 1; r < rl->num; r++)
		rectangle_join(a, &rl->rect[r]);

	return a;
}

/*
 * horizontal and vertical extents of a rectangle list
 */
RectangleList *rectanglelist_directionalextents(RectangleList *src,
		int (*compare)(PopplerRectangle *a, PopplerRectangle *b),
		gboolean (*touch)(PopplerRectangle *, PopplerRectangle *)) {
	int i, j;
	RectangleList *sorted, *dst;

	sorted = rectanglelist_copy(src);
	if (sorted->num == 0)
		return sorted;
	qsort(sorted->rect, sorted->num, sizeof(PopplerRectangle),
		(int (*)(const void *, const void *)) compare);

	dst = rectanglelist_new(sorted->num);
	for (i = 0; i < sorted->num; i++) {
		j = dst->num - 1;
		if (dst->num != 0 && touch(&dst->rect[j], &sorted->rect[i]))
			rectangle_join(&dst->rect[j], &sorted->rect[i]);
		else {
			dst->rect[dst->num] = sorted->rect[i];
			dst->num++;
		}
	}

	rectanglelist_free(sorted);

	return dst;
}
RectangleList *rectanglelist_hextents(RectangleList *src) {
	return rectanglelist_directionalextents(src,
		rectangle_hcompare, rectangle_htouch);
}
RectangleList *rectanglelist_vextents(RectangleList *src) {
	return rectanglelist_directionalextents(src,
		rectangle_vcompare, rectangle_vtouch);
}

/*
 * total width and height of a rectangle list
 */
double rectanglelist_sum(RectangleList *rl,
		double (*measure)(PopplerRectangle *)) {
	double res = 0;
	int i;
	for (i = 0; i < rl->num; i++)
		res += measure(&rl->rect[i]);
	return res;
}
double rectanglelist_sumwidth(RectangleList *rl) {
	return rectanglelist_sum(rl, rectangle_width);
}
double rectanglelist_sumheight(RectangleList *rl) {
	return rectanglelist_sum(rl, rectangle_height);
}

/*
 * average width and height of a rectangle list
 */
double rectanglelist_average(RectangleList *rl,
		double (*measure)(PopplerRectangle *)) {
	int r;
	double sum;

	sum = 0;
	for (r = 0; r < rl->num; rl++)
		sum += measure(&rl->rect[r]);
	return sum / rl->num;
}
double rectanglelist_averagewidth(RectangleList *rl) {
	return rectanglelist_average(rl, rectangle_width);
}
double rectanglelist_averageheight(RectangleList *rl) {
	return rectanglelist_average(rl, rectangle_height);
}

/*
 * index of rectangle in a list containing another rectangle
 */
gint rectanglelist_contain(RectangleList *rl, PopplerRectangle *r) {
	gint index;
	for (index = 0; index < rl->num; index++)
		if (rectangle_contain(&rl->rect[index], r))
			return index;
	return -1;
}

/*
 * index of rectangle in a list touching another rectangle
 */
gint rectanglelist_touch(RectangleList *rl, PopplerRectangle *r) {
	gint index;
	for (index = 0; index < rl->num; index++)
		if (rectangle_touch(&rl->rect[index], r))
			return index;
	return -1;
}

/*
 * index of rectangle in a list overlapping another rectangle
 */
gint rectanglelist_overlap(RectangleList *rl, PopplerRectangle *r) {
	 gint index;
	 for (index = 0; index < rl->num; index++)
		 if (rectangle_overlap(&rl->rect[index], r))
			 return index;
	 return -1;
}

/*
 * sort a rectangle list by position, quick and approximate
 */
void rectanglelist_quicksort(RectangleList *rl, PopplerPage *page) {
	(void) page;
	qsort(rl->rect, rl->num, sizeof(PopplerRectangle),
		(int (*)(const void *, const void *)) rectangle_compare);
}

/*
 * sort a rectangle list by position, in two steps
 */
void rectanglelist_twosort(RectangleList *rl, PopplerPage *page) {
	int i, j;
	int sorted;
	PopplerRectangle *r, *s;
	(void) page;

	/*
	 * sort vertically if horizontally overlapping
	 *
	 * iteratively find the minimum among i+1...rl->num-1
	 *
	 * when replacing the current minimum restart since the order is not
	 * transitively closed; e.g., {2, 0, 1} with 0<1 and 1<2 but not 0<2;
	 * without the restart the current minimum starts as 2, is not replaced
	 * by 0 but is then replaced by 1; minimum ends as 1 instead of 0
	 */
	for (i = 0; i < rl->num - 1; i++) {
		r = &rl->rect[i];
		for (j = i + 1; j < rl->num; j++) {
			s = &rl->rect[j];
			if (rectangle_htouch(r, s) &&
			    rectangle_vcompare(r, s) == 1) {
				r = s;
				j = i; // order is not transitivity closed
			}
		}
		rectangle_swap(&rl->rect[i], r);
	}

	/*
	 * sort horizontally, but respect the previous ordering
	 *
	 * this step is correct for the same reason of regular bubblesort: the
	 * first scan of the list moves the element that should go last at the
	 * end of the list; the same for the other scans for the remaining part
	 * of the list
	 *
	 * let k be the rectangle that should be placed last in the list: no
	 * other rectangle is greater than it according to the order < of the
	 * first step and no incomparable rectangle has greater x1
	 *
	 * the previous step placed all rectangles lower than k according to <
	 * before k in the list; for example, if i<j<k then i is before j and j
	 * is before k; the second step never swaps horizontally-overlapping
	 * rectangles, so it does not move i ahead of j and j ahead of k; as a
	 * result, k cannot be overtaken by a rectangle that is directly (like
	 * j) or indirectly (like i) lower than it according to the order <; in
	 * the same way, a rectangle that is incomparable with k cannot
	 * overtake it since no incomparable rectangle has x1 larger than that
	 * of k by assumption; all of this proves that when the element
	 * preceding k is compared with k, they are not swapped
	 *
	 * as a result, the scan proceeds with k and not with its preceding
	 * element; since the elements following k are incomparable with k
	 * according to <, they all have lower x1; the scan therefore moves k
	 * to the last position
	 */
	for (i = 0, sorted = 0; i < rl->num && ! sorted; i++) {
		sorted = 1;
		for (j = 0; j < rl->num - 1; j++) {
			r = &rl->rect[j];
			s = &rl->rect[j + 1];
			if (rectangle_htouch(r, s))
				continue;
			if (rectangle_hcompare(r, s) != 1)
				continue;
			rectangle_swap(r, s);
			sorted = 0;
		}
	}
}

/*
 * sort rectangles according to the order of their characters in the page
 */
void rectanglelist_charsort(RectangleList *rl, PopplerPage *page) {
	PopplerRectangle *rect;
	unsigned i, n;
	int j, p;

	poppler_page_get_text_layout(page, &rect, &n);
	for (i = 0, p = 0; i < n && p < rl->num; i++)
		for (j = p; j < rl->num; j++)
			if (rectangle_contain(&rl->rect[j], &rect[i])) {
				rectangle_swap(&rl->rect[p], &rl->rect[j]);
				p++;
				break;
			}
	g_free(rect);
}

/*
 * position a rectangle in a page partially filled by others
 */
gboolean rectanglelist_place(PopplerRectangle *page,
		RectangleList *rl, PopplerRectangle *r,
		PopplerRectangle *moved) {
	PopplerRectangle or;
	gdouble x, y, minx, miny;
	gint index;

	rectangle_copy(&or, r);
	rectangle_shift(&or, -or.x1, -or.y1);

	for (y = page->y1; y + or.y2 <= page->y2; y = miny) {
		miny = page->y2;
		for (x = page->x1; x + or.x2 <= page->x2; x = minx) {
			rectangle_copy(moved, &or);
			rectangle_shift(moved, x, y);
			index = rectanglelist_overlap(rl, moved);
			if (index == -1)
				return TRUE;
			minx = rl->rect[index].x2;
			miny = MIN(miny, rl->rect[index].y2);
		}
	}

	return FALSE;
}

/*
 * append the subtraction of rectangle sub from list orig to list res:
 *	res += orig - sub
 *
 * for each rectangle in orig, subtract sub from it; this may generate up to
 * four rectangles, which are appended to list res if they contain rectangle
 * cont (if not NULL) and satisfy bounds b (if not NULL)
 */
gboolean rectanglelist_subtract_append(RectangleList *dest,
		RectangleList *orig, PopplerRectangle *sub,
		PopplerRectangle *cont, RectangleBound *b) {
	gint i;
	PopplerRectangle *a, *r;

	for (i = 0; i < orig->num; i++) {
		a = orig->rect + i;

		r = dest->rect + dest->num;
		r->x1 = a->x1;
		r->y1 = a->y1;
		r->x2 = MIN(a->x2, sub->x1);
		r->y2 = a->y2;
		if (rectangle_boundcontain(r, cont, b))
			if (! rectanglelist_add(dest, r))
				return FALSE;

		r = dest->rect + dest->num;
		r->x1 = a->x1;
		r->y1 = a->y1;
		r->x2 = a->x2;
		r->y2 = MIN(a->y2, sub->y1);
		if (rectangle_boundcontain(r, cont, b))
			if (! rectanglelist_add(dest, r))
				return FALSE;

		r = dest->rect + dest->num;
		r->x1 = MAX(a->x1, sub->x2);
		r->y1 = a->y1;
		r->x2 = a->x2;
		r->y2 = a->y2;
		if (rectangle_boundcontain(r, cont, b))
			if (! rectanglelist_add(dest, r))
				return FALSE;

		r = dest->rect + dest->num;
		r->x1 = a->x1;
		r->y1 = MAX(a->y1, sub->y2);
		r->x2 = a->x2;
		r->y2 = a->y2;
		if (rectangle_boundcontain(r, cont, b))
			if (! rectanglelist_add(dest, r))
				return FALSE;
	}

	return TRUE;
}

/*
 * subtract a rectangle list from another: orig -= sub
 */
gboolean rectanglelist_subtract(RectangleList **orig, RectangleList *sub,
		PopplerRectangle *cont, RectangleBound *b) {
	RectangleList *dest;
	gint r;
	RectangleBound bd = {0.0, 0.0};

	for (r = 0; r < sub->num; r++) {
		dest = rectanglelist_new(MAXRECT);
		if (! rectanglelist_subtract_append(dest, *orig, sub->rect + r,
		                                    cont, b != NULL ? b : &bd))
			return FALSE;
		if (debugtextrectangles == -1 && dest->num != (*orig)->num)
			printf("rectangles: %d\n", dest->num);
		rectanglelist_free(*orig);
		*orig = dest;
	}

	return TRUE;
}

/*
 * subtract a rectangle list from a single rectangle: res = r - rl
 */
RectangleList *rectanglelist_subtract1(PopplerRectangle *r, RectangleList *rl,
		PopplerRectangle *cont, RectangleBound *b) {
	RectangleList *res;

	res = rectanglelist_new(MAXRECT);
	rectangle_copy(res->rect, r);
	res->num = 1;

	if (! rectanglelist_subtract(&res, rl, cont, b)) {
		rectanglelist_free(res);
		return NULL;
	}
	return res;
}

/*
 * join consecutive touching rectangles of a list
 */
void rectanglelist_consecutive(RectangleList *orig) {
	gint i, j;

	if (orig->num == 0)
		return;

	for (j = 0, i = 1; i < orig->num; i++)
		if (rectangle_touch(orig->rect + j, orig->rect + i))
			rectangle_join(orig->rect + j, orig->rect + i);
		else {
			j++;
			rectangle_copy(orig->rect + j, orig->rect + i);
		}
	orig->num = j + 1;
}

/*
 * join touching rectangles in a rectangle list
 */
void rectanglelist_join(RectangleList *orig) {
	gint i, j, n;

	/* why the do-while loop: joining may produce a rectangle that overlaps
	 * a previous one, for example:
	 *	1 2
	 *	  3
	 *	654
	 * rectangle 1 does not touch any else; but then 2 is joined to 3, then
	 * 4, 5 and 6; the resulting rectangle includes 1, since joining two
	 * rectangles produces a rectangle that includes both */

	do {
		n = orig->num;
		for (i = 0; i < orig->num; i++)
			for (j = i + 1; j < orig->num; j++)
				if (rectangle_touch(orig->rect + i,
						orig->rect + j)) {
					rectangle_join(orig->rect + i,
						orig->rect + j);
					rectanglelist_delete(orig, j);
					j = i;
				}
	} while (n != orig->num);
}

/*
 * the rectangles of the single characters in the page
 */
RectangleList *rectanglelist_characters(PopplerPage *page) {
	RectangleList *layout;
	char *text, *cur, *next;
	guint n;
	gint r;

	layout = rectanglelist_new(0);
	poppler_page_get_text_layout(page, &layout->rect, &n);
	layout->num = n;
	text = poppler_page_get_text(page);

	/* nullify rectangles of white spaces ' '; yes, it happens */
	for (r = 0, cur = text; r < layout->num; r++, cur = next) {
		next = g_utf8_next_char(cur);
		if (*cur == ' ')
			layout->rect[r].x2 = layout->rect[r].x1;
	}

	free(text);
	return layout;
}

/*
 * the area used by text in the page
 * the gdouble parameters define the minimal size of considered rectangles
 */
RectangleList *rectanglelist_textarea_bound(PopplerPage *page,
		RectangleList *layout,
		gdouble whiteboth, gdouble whiteeach,
		gdouble blackboth, gdouble blackeach) {
	PopplerRectangle p;
	RectangleList *white, *black;
	RectangleBound wb, bb;

	wb.both = whiteboth;
	wb.each = whiteeach;
	bb.both = blackboth;
	bb.each = blackeach;

	if (debugtextrectangles)
		printf("character rectangles: %d\n", layout->num);
	if (debugtextrectangles == 1)
		return layout;

	rectanglelist_consecutive(layout);
	if (debugtextrectangles)
		printf("consecutive rectangles: %d\n", layout->num);
	if (debugtextrectangles == 2)
		return layout;

	rectangle_page(page, &p);
	p.x1 -= wb.both - 1.0;	/* enlarge, otherwise thin white areas at */
	p.y1 -= wb.both - 1.0;  /* the borders are lost */
	p.x2 += wb.both + 1.0;
	p.y2 += wb.both + 1.0;
	white = rectanglelist_subtract1(&p, layout, NULL, &wb);
	if (white == NULL)
		return NULL;
	if (debugtextrectangles)
		printf("white rectangles: %d\n", white->num);
	rectanglelist_free(layout);
	if (debugtextrectangles == 3)
		return white;

	rectangle_page(page, &p);
	black = rectanglelist_subtract1(&p, white, NULL, &bb);
	if (black == NULL)
		return NULL;
	if (debugtextrectangles)
		printf("white rectangles: %d\n", black->num);
	rectanglelist_free(white);
	if (debugtextrectangles == 4)
		return black;

	rectanglelist_join(black);
	if (debugtextrectangles)
		printf("joined rectangles: %d\n", black->num);

	rectanglelist_tighten(black);
	return black;
}

/*
 * the area used by text in the page, with fallback to whole page
 */
RectangleList *rectanglelist_textarea_bound_fallback(PopplerPage *page,
		RectangleList *layout,
		gdouble whiteboth, gdouble whiteeach,
		gdouble blackboth, gdouble blackeach) {
	RectangleList *res;
	res = rectanglelist_textarea_bound(page, layout,
			whiteboth, whiteeach, blackboth, blackeach);
	if (res != NULL)
		return res;

	/* fallback: finding the rectangle list was impossible because of the
	 * large number of rectangles; just return the whole page */
	res = rectanglelist_new(1);
	rectangle_page(page, res->rect);
	res->num = 1;
	return res;
}

/*
 * text area in the page, with parametric minimal distance considered a space
 */
RectangleList *rectanglelist_textarea_distance(PopplerPage *page, gdouble w) {
	RectangleList *layout;
	layout = rectanglelist_characters(page);
	return rectanglelist_textarea_bound_fallback(page, layout,
			w, 100.0, 0.0, 0.0);
}

/*
 * text area in the page
 */
RectangleList *rectanglelist_textarea(PopplerPage *page) {
	return rectanglelist_textarea_distance(page, 15.0);
}

/*
 * bounding box of a page (NULL if no text is in the page)
 */
PopplerRectangle *rectanglelist_boundingbox(PopplerPage *page) {
	RectangleList *all;
	PopplerRectangle *boundingbox;
	guint n;

	all = rectanglelist_new(0);
	poppler_page_get_text_layout(page, &all->rect, &n);
	if (n == 0)
		return NULL;
	all->num = n;

	boundingbox = rectanglelist_joinall(all);

	rectanglelist_free(all);
	return boundingbox;
}

/*
 * overall bounding box of the whole document (NULL if no text)
 */
PopplerRectangle *rectanglelist_boundingbox_document(PopplerDocument *doc) {
	PopplerPage *page;
	PopplerRectangle *boundingbox, *pageboundingbox;
	int npages, n;

	npages = poppler_document_get_n_pages(doc);
	if (npages < 0)
		return NULL;
	boundingbox = NULL;
	for (n = 0; n < npages; n++) {
		page = poppler_document_get_page(doc, n);
		pageboundingbox = rectanglelist_boundingbox(page);
		if (pageboundingbox == NULL)
			continue;
		if (boundingbox == NULL)
			boundingbox = pageboundingbox;
		else {
			rectangle_join(boundingbox, pageboundingbox);
			poppler_rectangle_free(pageboundingbox);
		}
	}

	return boundingbox;
}

/*
 * list of squares of a grid that are painted in a page
 */
RectangleList *rectanglelist_painted(PopplerPage *page, int distance) {
	double width, height;
	cairo_surface_t *surface;
	cairo_t *cr;
	unsigned char *data;
	int w, h, stride, x, y;
	RectangleList *painted;
	PopplerRectangle *r;

	poppler_page_get_size(page, &width, &height);
	w = width / distance;
	h = height / distance;

	surface = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
	cr = cairo_create(surface);
	cairo_scale(cr, w / width, h / height);
	poppler_page_render_for_printing(page, cr);
	cairo_surface_show_page(surface);

	data = cairo_image_surface_get_data(surface);
	stride = cairo_image_surface_get_stride(surface);
	cairo_surface_flush(surface);

	painted = rectanglelist_new(MAXRECT);

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			if (data[stride * y + x ] == 0)
				continue;
			r = poppler_rectangle_new();
			r->x1 = x * distance;
			r->y1 = y * distance;
			r->x2 = r->x1 + distance;
			r->y2 = r->y1 + distance;
			rectanglelist_append(painted, r);
		}

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	return painted;
}

/*
 * area of painted squares in a page, with minimal distance of white space
 */
RectangleList *rectanglelist_paintedarea_distance(PopplerPage *page,
		gdouble w) {
	RectangleList *layout;
	layout = rectanglelist_painted(page, w);
	return rectanglelist_textarea_bound(page, layout, w, 100.0, 0.0, 0.0);
}

/*
 * bounding box of a page, based on painted squares
 */
PopplerRectangle *rectanglelist_boundingbox_painted(PopplerPage *page,
		int distance) {
	RectangleList *layout;
	PopplerRectangle *boundingbox;

	layout = rectanglelist_painted(page, distance);
	boundingbox = rectanglelist_joinall(layout);
	rectanglelist_free(layout);
	return boundingbox;
}

/*
 * draw a rectangle on a cairo context with a random color
 */
void rectangle_draw(cairo_t *cr, PopplerRectangle *rect,
		gboolean randomcolor, gboolean fill, gboolean enclosing) {
	double enlarge = 0.0;

	if (rect == NULL)
		return;

	if (! randomcolor)
		cairo_set_source_rgb(cr, 0.8, 0.8, 1.0);
	else
		cairo_set_source_rgb(cr,
			((gdouble) random()) / RAND_MAX * 0.8,
			((gdouble) random()) / RAND_MAX * 0.8,
			((gdouble) random()) / RAND_MAX * 0.8);
	if (enclosing)
		enlarge = cairo_get_line_width(cr) / 2;
	cairo_rectangle(cr,
		rect->x1 - enlarge,
		rect->y1 - enlarge,
		rect->x2 - rect->x1 + enlarge * 2,
		rect->y2 - rect->y1 + enlarge * 2);
	if (fill)
		cairo_fill(cr);
	cairo_stroke(cr);
}

/*
 * draw a rectangle list on a cairo context
 */
void rectanglelist_draw(cairo_t *cr, RectangleList *rl,
		gboolean fill, gboolean enclosing,
		gboolean num, gboolean inside) {
	gint r;
	char buf[20];

	for (r = 0; r < rl->num; r++) {
		rectangle_draw(cr, rl->rect + r, TRUE, fill, enclosing);
		if (num) {
			cairo_move_to(cr,
				rl->rect[r].x1 + (inside ? 10 : -10.0),
				rl->rect[r].y1 + 10.0);
			sprintf(buf, "%d", r);
			cairo_show_text(cr, buf);
		}
	}
}

/*
 * apply the current transformation to a rectangle
 */
void rectangle_transform(cairo_t *cr, PopplerRectangle *r) {
	cairo_user_to_device(cr, &r->x1, &r->y1);
	cairo_user_to_device(cr, &r->x2, &r->y2);
}

/*
 * map a poppler rectangle into a cairo surface
 *
 * do no rendering, just modify the transformation matrix so that operations on
 * the cairo contex are done with the coordinates of the poppler page
 *
 * for example, poppler_page_render_for_printing(page, cr) renders the
 * rectangle src in the page as dst on the cairo context
 *
 * horizontal: scale so that src fits into dst horizontally
 * ratio: preserve aspect ratio
 * topalign: map so that that dst maps to the top of src
 *
 * call cairo_identity_matrix(cr) to reset the transformation matrix
 */
void rectangle_map_to_cairo(cairo_t *cr,
		PopplerRectangle *dst, PopplerRectangle *src,
		gboolean horizontal, gboolean vertical,
		gboolean ratio, gboolean topalign, gboolean leftalign) {
	gdouble srcw, srch;
	gdouble dstw, dsth;
	gdouble scalex, scaley;
	gdouble marginx, marginy;

	if (src == NULL || dst == NULL)
		return;

	srcw = src->x2 - src->x1;
	srch = src->y2 - src->y1;
	dstw = dst->x2 - dst->x1;
	dsth = dst->y2 - dst->y1;

	scalex = dstw / srcw;
	if (horizontal)
		scaley = scalex;
	else {
		scaley = dsth / srch;
		if (vertical)
			scalex = scaley;
		else {
			if (ratio && scalex > scaley)
				scalex = scaley;
			if (ratio && scaley > scalex)
				scaley = scalex;
		}
	}

	marginx = dst->x1 + (dstw - srcw * scalex) / 2;
	marginy = dst->y1 + (dsth - srch * scaley) / 2;
	if (topalign)
		marginy = dst->y1;
	if (leftalign)
		marginx = dst->x1;

	/* transformations work like a stack:
	 * the last added is applied first */
	cairo_translate(cr, marginx, marginy);
	cairo_scale(cr, scalex, scaley);
	cairo_translate(cr, -src->x1, -src->y1);
}

/*
 * paper size
 */
struct
{	char *name;		PopplerRectangle size;		}
papersize[] = {
{	"Letter",		{0.0, 0.0,  612.0,  792.0}	},
{	"LetterSmall",		{0.0, 0.0,  612.0,  792.0}	},
{	"Tabloid",		{0.0, 0.0,  792.0, 1224.0}	},
{	"Ledger",		{0.0, 0.0, 1224.0,  792.0}	},
{	"Legal",		{0.0, 0.0,  612.0, 1008.0}	},
{	"Statement",		{0.0, 0.0,  396.0,  612.0}	},
{	"Executive",		{0.0, 0.0,  540.0,  720.0}	},
{	"Folio",		{0.0, 0.0,  612.0,  936.0}	},
{	"Quarto",		{0.0, 0.0,  610.0,  780.0}	},
{	"10x14",		{0.0, 0.0,  720.0, 1008.0}	},

{	"A0",			{0.0, 0.0, 2384.0, 3371.0}	},
{	"A1",			{0.0, 0.0, 1685.0, 2384.0}	},
{	"A2",			{0.0, 0.0, 1190.0, 1684.0}	},
{	"A3",			{0.0, 0.0,  842.0, 1190.0}	},
{	"A4",			{0.0, 0.0,  595.0,  842.0}	},
{	"A5",			{0.0, 0.0,  420.0,  595.0}	},
{	"A6",			{0.0, 0.0,  298.0,  420.0}	},
{	"A7",			{0.0, 0.0,  210.0,  298.0}	},
{	"A8",			{0.0, 0.0,  148.0,  210.0}	},
{	"A9",			{0.0, 0.0,  105.0,  147.0}	},
{	"A10",			{0.0, 0.0,   74.0,  105.0}	},

{	"B0",			{0.0, 0.0, 2835.0, 4008.0}	},
{	"B1",			{0.0, 0.0, 2004.0, 2835.0}	},
{	"B2",			{0.0, 0.0, 1417.0, 2004.0}	},
{	"B3",			{0.0, 0.0, 1001.0, 1417.0}	},
{	"B4",			{0.0, 0.0,  729.0, 1032.0}	},
{	"B5",			{0.0, 0.0,  516.0,  729.0}	},
{	"B6",			{0.0, 0.0,  354.0,  499.0}	},
{	"B7",			{0.0, 0.0,  249.0,  354.0}	},
{	"B8",			{0.0, 0.0,  176.0,  249.0}	},
{	"B9",			{0.0, 0.0,  125.0,  176.0}	},
{	"B10",			{0.0, 0.0,   88.0,  125.0}	},

{	"C0",			{0.0, 0.0, 2599.0, 3677.0}	},
{	"C1",			{0.0, 0.0, 1837.0, 2599.0}	},
{	"C2",			{0.0, 0.0, 1837.0,  578.0}	},
{	"C3",			{0.0, 0.0,  578.0,  919.0}	},
{	"C4",			{0.0, 0.0,  919.0,  649.0}	},
{	"C5",			{0.0, 0.0,  649.0,  459.0}	},
{	"C6",			{0.0, 0.0,  459.0,  323.0}	},
{	"C7",			{0.0, 0.0,  230.0,  323.0}	},
{	"C8",			{0.0, 0.0,  162.0,  230.0}	},
{	"C9",			{0.0, 0.0,  113.0,  162.0}	},
{	"C10",			{0.0, 0.0,   79.0,  113.0}	},

{	NULL,			{0.0, 0.0,    0.0,    0.0}	}
};

/*
 * from name to paper size
 */
PopplerRectangle *get_papersize(char *name) {
	int i;

	for (i = 0; papersize[i].name; i++)
		if (! strcasecmp(papersize[i].name, name))
			return &papersize[i].size;

	return NULL;
}

/*
 * default paper size
 */
char *defaultpapersize() {
	FILE *fd;
	char s[100], *r;
	char *res;

	fd = fopen("/etc/papersize", "r");
	if (fd == NULL)
		return NULL;

	r = malloc(100);
	do {
		res = fgets(s, 90, fd);
		if (res == NULL) {
			fclose(fd);
			free(r);
			return NULL;
		}
		res = strchr(s, '#');
		if (res)
			*res = '\0';
	} while (sscanf(s, "%90s", r) != 1);

	fclose(fd);
	return r;
}

/*
 * escape filenames
 */
char *filenameescape(char *filename) {
	char *res;
	int i, j;

	res = malloc(strlen(filename) * 3 + 1);
	for (i = 0, j = 0; filename[i] != '\0'; i++)
		if (filename[i] >= 32 && filename[i] != '%')
			res[j++] = filename[i];
		else {
			sprintf(res + j, "%%%02X", filename[i]);
			j += 3;
		}
	res[j] = '\0';

	return res;
}

/*
 * from file name to uri
 */
char *filenametouri(char *filename) {
	char *dir, *sep, *esc, *uri;

	if (filename[0] == '/') {
		dir = strdup("");
		sep = "";
	}
	else {
		dir = malloc(4096);
		if (dir == NULL) {
			printf("failed to allocate memory for directory\n");
			return NULL;
		}
		if (getcwd(dir, 4096) == NULL) {
			printf("error in obtaining the current directory\n");
			return NULL;
		}
		sep = "/";
	}

	esc = filenameescape(filename);

	uri = malloc(strlen("file:") + strlen(dir) +
		strlen(sep) + strlen(esc) + 1);
	if (uri == NULL) {
		printf("failed to allocate memory for file name\n");
		free(esc);
		return NULL;
	}
	strcpy(uri, "file:");
	strcat(uri, dir);
	strcat(uri, sep);
	strcat(uri, esc);

	free(esc);
	free(dir);
	return uri;
}

/*
 * add suffix to a pdf filename
 */
char *pdfaddsuffix(char *infile, char *suffix) {
	char *outfile;
	char *pos;

	outfile = malloc(strlen(infile) + strlen(suffix) + 10);
	strcpy(outfile, basename(infile));
	pos = strrchr(outfile, '.');
	if (pos != NULL && (! strcmp(pos, ".pdf") || ! strcmp(pos, ".PDF")))
		*pos = '\0';

	strcat(outfile, "-");
	strcat(outfile, suffix);
	strcat(outfile, ".pdf");
	return outfile;
}


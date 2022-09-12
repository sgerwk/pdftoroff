/*
 * hovacui.c
 *
 * view a pdf document, autozooming to text
 */

/*
 * todo:
 *
 * - search: regular expression; config for pattern=string
 * - configuration files specific for the framebuffer and x11, passed as
 *   additional arguments to hovacui()
 *   .config/hovacui/{framebuffer.conf,x11.conf}
 * - merge narrow textarea boxes with others, minimizing size increase
 * - merge boxes with the same (or very similar) horizontal coordinates
 * - bookmarks, with field() for creating and list() for going to
 * - save last position(s) to $HOME/.pdfpositions
 * - commandline option for initial position: -p page,box,scrollx,scrolly any
 *   part can be empty, even page; every one implies a default for the
 *   folliowing ones; if this option is given, the final position is printed in
 *   the same format at exit; make separate functions for parsing and printing
 *   a struct position; the function for parsing takes care of illegal values:
 *   for example, a page number too large is reduced to the last, and
 *   consequently the box is the last of the page; extract and complete this
 *   part of the code from reloadpdf()
 * - maintain the position when changing mode: choose a character that is close
 *   to the center of the screen before changing mode; afterward, select the
 *   box that contains it, set scrollx,scrolly so that the character is in the
 *   center of the screen; then adjust the scroll
 * - save an arbitrary selection/order of pages to file, not just a range;
 *   allow moving when chop() is active;
 *
 * - keystroke 'O' for setting the current page as page 1, like -O;
 *   same by a entry in the main menu; or field for changing the page offset
 * - printf format string for page number label, with total pages
 * - make minwidth depend on the size of the letters
 * - list() yes/no to confirm exit; disabled by config option
 * - allow reloading during search and save
 * - fail search and save if the document change during them
 * - man page: compare with fbpdf and jfbview
 * - field() for executing a shell command
 * - info(), based on list(): filename, number of pages, page size, etc.
 * - rotate
 * - lines of previous and next scroll: where the top or bottom of the screen
 *   were before the last scroll, or will be after scrolling up or down
 * - stack of windows; a window returns WINDOW_PREVIOUS to go back
 * - cache the textarea list of pages already scanned
 * - config opt "nolabel" for no label at all: skip the label part from draw()
 * - multiple files, list()-based window; return WINDOW_FILE+n to tell main()
 *   which file to switch to; and/or have a field in struct output for the new
 *   file index or name
 * - optionally, show opening error as a window instead of fprintf
 * - allow for a password (field() or commandline)
 * - last two require document() to show a black screen if no document is open
 * - non-expert mode: every unassigned key calls WINDOW_MENU
 * - clip window title in list()
 * - history of searches
 * - include images (in pdfrects.c)
 * - key to reset viewmode and fit direction to initial values
 * - history of positions
 * - order of rectangles for right-to-left and top-to-bottom scripts
 *   (generalize sorting functions in pdfrects.c)
 * - i18n
 * - annotations and links:
 *   some key switches to anchor navigation mode, where keyup/keydown or n/p
 *   move to the next anchor (annotation or link) in the displayed part of the
 *   current textbox if any, otherwise they scroll as usual; this requires
 *   storing the current anchor both for its attribute (the text or the target)
 *   and for moving to the next; use movetoselected() with afterscreen=FALSE
 *   and current!=CURRENT_UNUSED for searching in the visible part of the
 *   current textbox; if search fails scroll, but then search again for an
 *   annotation or link in the new position
 * - detect file changes via inotify; this requires the file descriptor to be
 *   passed to cairodevice->input(); return KEY_FILECHANGE; the pdf file may
 *   not being fully written at that point, which results in a sequence of
 *   opening failures before being able to read it again; the current method
 *   using SIGHUP is better, if there is some way to send a signal when the
 *   file change is completed
 * - make the margins (what determine the destination rectangle) customizable
 *   by field() or configuration file
 * - alternative to fit=none: wrap the lines that are longer than the minwidth
 * - next/previous match does not work with fit=none; do not fix, cannot work
 *   in general (see below); it can however be done by the same system of the
 *   next or previous anchor used for annotations and links
 * - prefer showing white area outside bounding box than outside page
 * - thread for page rendering, so that the ui remains responsive even on
 *   complex pages; the page and the ui render on different surfaces, that are
 *   then copied to the output
 * - thread for progress indicator: tells that the program is still working,
 *   and nothing is shown because rendering or saving is in progress
 *
 * regressions:
 * - open a long document, open gotopage window, replace file with a short
 *   document, go to a page the previous document has but the new does not
 */

/*
 * how the document is mapped onto the screen
 * ------------------------------------------
 *
 * position->textarea->rect[position->box]
 *	the current textbox: the block of text that is focused (drawn in blue)
 *
 * position->viewbox
 *	the same rectangle, possibly enlarged to ensure the minimal width
 *
 * output->dest
 *	the area of the screen to use (all of it but a thin margin)
 *
 * the cairo transformation matrix is initially set so that position->viewbox
 * (rectangle in the document) is mapped onto output->dest (rectangle in the
 * screen), fitted by width and aligned to the top
 *
 * position->scrolly
 *	the shift applied to the document;
 *	used to move the viewbox up and down
 *
 * shifting by the scroll is done last; since transformations work as if
 * applied to the source in reverse order, this is like first scrolling the
 * document and then mapping position->viewbox onto the top of output->dest
 *
 * the textbox is at the center of the viewbox and is therefore shown centered
 * on the screen; but position->scrollx and position->scrolly are adjusted to
 * avoid parts outside of the bounding box being displayed, wasting screen
 * space; in this case, the textbox is moved out of the center of the screen;
 * this happens for example for the textboxes at the border of the page
 *
 * all of this is for horizontal fitting mode: vertical fitting mode fits the
 * viewbox by height, but is otherwise the same; in both modes, the scroll is
 * relative to the origin of the current viewbox
 */

/*
 * search
 * ------
 *
 * output->search
 *	the last searched string;
 *	needed to highlight the matches when switching pages
 *
 * output->forward
 *	whether to move forward or backward between search matches
 *
 * output->found
 *	the list of rectangles of the search matches in the current page
 *
 * a match is located by scanning the document from the current textbox to the
 * last of the page, then in the following pages, counting the number of pages
 * until reaching the number of pages in the document
 *
 * positionscan()
 *	the document is scanned by a temporary struct position; this function
 *	initializes it as a copy of the current position, copies it back as the
 *	current position (done if a match is found) and deallocates it
 *
 * movetoselected()
 * beforescreen
 * inscreen
 * afterscreen
 *	move to the first or next match; the three arguments tell whether to
 *	consider matches that are before, inside or after the visible part of
 *	the current textbox; beforescreen implies inscreen
 *
 *	the visible part of the current textbox is considered when looking for
 *	the first match ('/') but skipped when looking for the next ('n'); what
 *	over the visible part of the current textbox is initially ignored, and
 *	considered after switching to the following boxes or pages; afterscreen
 *	is always true for searching; it is intended for next-or-scroll moves:
 *	go to the next match if in the screen, otherwise scroll
 *
 * output->current
 *	if not CURRENT_UNUSED, matches are navigated one by one instead of a
 *	screenful at time; when the current match is in the visible part of the
 *	current textbox, the next match is the one following it (no matter
 *	where it is); otherwise, the next match is the first starting from the
 *	top of the visible part of the current textbox; the box that is
 *	searched in is the current textbox if beforescreen==FALSE
 */

/*
 * note: fit=none
 *
 * fit=none is mostly an hack to allow for arbitrary zooming and moving in the
 * page like regular pdf viewers
 *
 * it is implemented in the current framework by allowing the viewbox to be
 * narrower than the textbox, instead of always wider or equally wide; since
 * the viewbox is mapped to the screen, this allows the screen to show only a
 * part of the width of the textbox instead of its full width
 *
 * this requires toptextbox() to decrease the scroll: a zero scroll would just
 * map the viewbox to the screen, but now the viewbox is somewhere in the
 * middle of the textbox; while its upper left corner should be shown instead;
 * the same for bottomtextbox()
 *
 * the current method for finding the next or previous search match cannot work
 * in general: in the other fit modes, the next match in the current textbox is
 * the first match below or on the right of the portion of the textbox that is
 * currently shown; with fit=none, that portion may be the middle of the
 * textbox; depending on how the next match is searched for, two matches in the
 * lower left corner and the upper right corner may be each the next of the
 * other, or none the next of the other
 */

/*
 * note: reload
 *
 * a file is reloaded in the main loop whenever cairoui->reload=TRUE
 *
 * this field is set in:
 *
 * - document(), upon receiving keystroke 'r'
 * - external(), upon receiving the external command "reload"
 * - draw() or textarea() if a file change is detected (see below)
 *
 * file changes are detected via poppler_document_get_id(), but this only works
 * after trying to render the document; this is why detection is done in draw()
 *
 * this means that the document is automatically reloaded when switching into
 * the virtual terminal and when moving in the document; it is not reloaded
 * otherwise, in particular it is not reloaded when a window is active (unless
 * it requests a refresh)
 *
 * since the newly opened document may have fewer pages than the previous,
 * checking POPPLER_IS_PAGE(position->page) is always necessary before
 * accessing the page (reading the textarea or drawing the page); if the page
 * does not exist, output->reload is set
 */

/*
 * note: the minwidth
 *
 * the minwidth avoids textboxes being zoomed too much; without it, a narrow
 * textbox such as a page number would render too large
 *
 * precisely, the minwidth is the minimal width of a textbox that is zoomed to
 * fit the width of the destination box; a narrower texbox is mapped to a box
 * that is proportionately narrower than the destination box
 *
 * for example, the default 400 means that a textbox in the document that is
 * wide 400 pdf points is zoomed to fit to the width of the destination box,
 * and that narrower boxes are zoomed the same and therefore take less than the
 * width of the destination box
 *
 * in x11 the window (destination box) may be small or large; in a small window
 * even a narrow box may need to be zoomed to the window width to be readable;
 * for this reason, the minwidth is reduced in proportion to the window size;
 * thanks to this multiplication, enlarging a small window the current textbox
 * is enlarged, but at at some points the zoom stops increasing
 */

/*
 * note: the page offset
 *
 * pages are numbered from 0 to numpages-1 in poppler but the page numbers as
 * printed in them go from 1 up; prefaces, tables of content and other material
 * may make page 1 not the first in the document; files that are cut from
 * longer documents may begin with a page number larger than one
 *
 * this creates a user interface misaligment; for example, the expected result
 * of jumping to page 20 is to see the page with "20" printed at the bottom,
 * not "12" or "24", even if the page marked "12" or "24" is the twenty-th page
 * of the document
 *
 * for this reason, pages in the user interface are as expected by the user;
 * they start with page 1, not 0; this is the default, it can be changed by the
 * -O and -F options
 *
 * internally, page numbers are always stored as poppler pages, from 0 to
 * numpages-1; they are translated only when shown to the user or input from
 * the user; the output structure contains an offset field that stores the
 * difference; conversion is done by pagepdftoui() and pageuitopdf()
 *
 * a similar conversion is also done for internal references in
 * movetonameddestination(), as destinations in pdf always use the
 * 1-to-numpages numbering
 */

/*
 * note: step-by-step operations
 *
 * some operations like searching and saving a range of pages may take long,
 * and the interface looks dead meanwhile; this is avoided by turning the loop
 * on pages into a page-by-page sequence of calls, returning to the main loop
 * at each iteration to display progress and to check input for a request to
 * abort (escape key)
 *
 * if the operation is done by a function, the window looks like this:
 *
 *	int window(int c, struct cairoui *cairoui) {
 *		...
 *
 *		init;
 *
 *		res = template(c, cairoui, ...);
 *		...
 *		if (res == ... || c == ...) {
 *			pre;
 *			o = function(...);
 *			post;
 *			return WINDOW_DOCUMENT;
 *		}
 *		...
 *	}
 *
 * its step-by-step version turns function() into a sort of window, called
 * repeatedly until the operation completes; it is not called from the main
 * loop but from this window, which acts as a passthrough to function() until
 * finished; since function() is called from this window and not from the main
 * loop, the data it needs is just passed to it rather than being stored in the
 * output structure
 *
 *	int window(int c, struct cairoui *cairoui) {
 *		...
 *		static gboolean iterating = FALSE;
 *		static int res, savec;
 *		int currc;
 *
 *		if (iterating) {
 *			currc = c;
 *			c = savec;
 *			if (currc == KEY_REFRESH)
 *				template(KEY_REFRESH, cairoui, ...)
 *		}
 *		else {
 *			init;
 *			res = template(c, cairoui, ...);
 *		}
 *		...
 *		if (res == ... || c == ...) {
 *			if (! iterating) {
 *				pre;
 *				iterating = TRUE;
 *				savec = c;
 *				currc = KEY_INIT;
 *			}
 *			o = function(currc, cairoui, ...);
 *			if (! CAIROUI_OUT(o))
 *				return WINDOW_THIS;
 *			if (currc == KEY_FINISH)
 *				iterating = FALSE;
 *			post;
 *			return WINDOW_DOCUMENT;
 *		}
 *		...
 *	}
 *
 * if c is not used in the original window after calling template(c, ...), the
 * variables savec and currc and their assigments can be omitted and c used in
 * place of currc; the call template(KEY_REFRESH, ...) is only present if the
 * window is to be shown during iteration, and requires the window to return
 * CAIROUI_REFRESH instead of WINDOW_THIS when redrawing is necessary; this may
 * be the case for the first iteration to remove the existing labels
 *
 * function() is no longer a simple function but a window; it stores its data
 * between calls; for example, savepdf() is (simplified):
 *
 *	int savepdf(int c, struct cairoui *cairoui, ...) {
 *		static int page;
 *
 *		if (c == KEY_INIT) {
 *			open file
 *			create cairo surface and context
 *			page = first;
 *			cairoui->timeout = 0;
 *			return CAIROUI_CHANGED;
 *		}
 *
 *		if (c == '\033')
 *			return CAIROUI_LEAVE;
 *
 *		if (c != KEY_FINISH) {
 *			save page
 *			page++
 *			cairoui->timeout = 0;
 *			return page <= last ? CAIROUI_CHANGED : CAIROUI_DONE;
 *		}
 *
 *		deallocate surface and cairo context
 *		close file
 *		if (page <= last) {
 *			delete file
 *			return CAIROUI_LEAVE;
 *		}
 *		else {
 *			postsave
 *			return CAIROUI_DONE;
 *		}
 *	}
 *
 * always perform a single operation at time; in case of error do not jump to
 * the final part, but return CAIROUI_FAIL and wait for the final call with
 * KEY_FINISH to finalize (close the file, etc) and report the error; use a
 * static variable to remember this condition and possibly the cause of error
 *
 * if something is printed in the help label, cairoui->redraw=TRUE removes the
 * previous label; this requires redrawing the page, so it is best to avoid
 * doing it at each step; a better choice is to do it only for the first
 * iteration and to use labels that covers the previous during the steps;
 * cairoui->timeout=0 is necessary when returning to the main loop between
 * steps (that is, not at the end); it makes function() to be called again
 * immediately after checking input and redrawing the labels
 *
 * the search() and next() windows use gotopagematch() as their step function,
 * with its nsearched parameter as the key (0=init, -1=finish, otherwise>0);
 * their nsearched static variable stores the number of pages searched so far;
 * since the next() window is always iterating it does not need a boolean
 * variabile to tell whether it is
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include "pdfrects.h"
#include "cairoio.h"
#include "cairoui.h"
#include "hovacui.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#undef clear

/*
 * name of the program
 */
#define HOVACUI "hovacui"

/*
 * output parameters
 */
struct output {
	/* the cairo surface */
	cairo_t *cr;

	/* the destination rectangle */
	PopplerRectangle dest;

	/* the size of the whole screen */
	double screenwidth;
	double screenheight;
	double screenaspect;

	/* the pixel aspect */
	double aspect;

	/* the minimal textbox-to-textbox distance */
	int distance;

	/* the minimal width; tells the maximal zoom */
	int minwidth;

	/* zoom to: 0=text, 1=boundingbox, 2=page */
	int viewmode;

	/* fit horizontally (1), vertically (2) or both (3) */
	int fit;

	/* sorting algorithm */
	int order;

	/* scroll distance, as a fraction of the screen size */
	double scroll;

	/* page offset */
	int offset;

	/* show the ui (menu and help) */
	int ui;

	/* apply the changes immediately from the ui */
	int immediate;

	/* draw draw the textbox and page box */
	int drawbox;

	/* show the page number when it changes */
	int pagelabel;

	/* whether the document has to be reloaded */
	int *reload;

	/* labels */
	gboolean pagenumber;
	gboolean totalpages;
	gboolean showclock;
	gboolean showmode;
	gboolean showfit;
	gboolean filename;
	char help[80];

	/* search */
	char search[100];
	gboolean forward;
	GList *found;
	int current;

	/* selection */
	GList *selection;
	double texfudge;

	/* pdf output */
	char *pdfout;
	int first, last;
	char *postsave;

	/* external script */
	char *keys;
	char *script;
	cairo_rectangle_t *rectangle;
};

/*
 * a position within a document
 */
struct position {
	/* the poppler document */
	char *filename;
	PopplerDocument *doc;
	gchar *permanent_id, *update_id;

	/* the current page, its bounding box, the total number of pages */
	int npage, totpages;
	PopplerPage *page;
	PopplerRectangle *boundingbox;

	/* the text rectangle currently viewed, all of them in the page */
	RectangleList *textarea;
	int box;
	PopplerRectangle *viewbox;

	/* how much the viewbox is moved before being displayed */
	double scrollx;
	double scrolly;
};

/*
 * the callback data structure
 */
struct callback {
	struct output *output;
	struct position *position;
};
#define POSITION(cairoui) (((struct callback *) (cairoui)->cb)->position)
#define OUTPUT(cairoui)   (((struct callback *) (cairoui)->cb)->output)

/*
 * current match
 */
#define CURRENT_UNUSED (-2)
#define CURRENT_NONE (-1)
void setcurrent(int *current, int value) {
	if (*current != CURRENT_UNUSED)
		*current = value;
}

/*
 * convert between pdf internal page number and page number in the interface
 */
int pagepdftoui(struct output *output, int page) {
	return page - output->offset + 2;
}
int pageuitopdf(struct output *output, int page) {
	return page + output->offset - 2;
}

/*
 * initialize position
 */
void initposition(struct position *position) {
	position->npage = 0;
	position->page = NULL;
	position->boundingbox = NULL;
	position->textarea = NULL;
	position->box = 0;
	position->viewbox = NULL;
	position->scrollx = 0;
	position->scrolly = 0;
}

/*
 * initial page number
 */
void initpage(struct position *position, int npage) {
	position->npage = npage < 0 ? 0 :
		npage >= position->totpages ? position->totpages - 1 : npage;
}

/*
 * a rectangle as large as the whole page
 */
PopplerRectangle *pagerectangle(PopplerPage *page) {
	PopplerRectangle *r;
	r = poppler_rectangle_new();
	r->x1 = 0;
	r->y1 = 0;
	poppler_page_get_size(page, &r->x2, &r->y2);
	return r;
}

/*
 * free a glist of rectangles
 */
void freeglistrectangles(GList *list) {
	GList *l;
	for (l = list; l != NULL; l = l->next)
		poppler_rectangle_free((PopplerRectangle *) l->data);
	g_list_free(list);
}

/*
 * find the matches rectangles in the page
 */
int pagematch(struct position *position, struct output *output) {
	freeglistrectangles(output->found);
	output->found = output->search[0] == '\0' ?
		NULL : poppler_page_find_text(position->page, output->search);
	return 0;
}

/*
 * read the current page without its textarea
 */
int readpageraw(struct position *position, struct output *output) {
	g_set_object(&position->page,
		poppler_document_get_page(position->doc, position->npage));
	g_object_unref(position->page); // otherwise, it would be 2 now
	pagematch(position, output);
	freeglistrectangles(output->selection);
	output->selection = NULL;
	setcurrent(&output->current, CURRENT_NONE);
	return 0;
}

/*
 * how much the page is fragmented in the boxes of its textarea
 */
double fragmented(struct position *position) {
	RectangleList *rl;
	int i;
	double width, height, index;

	rl = position->textarea;
	width = position->boundingbox->x2 - position->boundingbox->x1;
	height = position->boundingbox->y2 - position->boundingbox->y1;
	index = 0;
	for (i = 0; i < rl->num; i++) {
		if (rectangle_width(&rl->rect[i]) < width / 6)
			index++;
		if (rectangle_height(&rl->rect[i]) < height / 6)
			index++;
	}

	return index / rl->num;
}

/*
 * how much the boxes of the text area overlap horizontally, if they do
 */
double interoverlap(struct position *position) {
	RectangleList *ve, *ta;
	int i, j;
	double height, index;

	ta = position->textarea;
	ve = rectanglelist_vextents(ta);
	height = rectanglelist_sumheight(ve);
	// height = position->boundingbox->y2 - position->boundingbox->y1;
	rectanglelist_free(ve);

	index = 0;
	for (i = 0; i < ta->num; i++)
		for (j = 0; j < ta->num; j++)
			if (! rectangle_htouch(&ta->rect[i], &ta->rect[j]))
				index +=
					(ta->rect[i].y2 - ta->rect[i].y1) /
						height *
					(ta->rect[j].y2 - ta->rect[j].y1) /
						height;

	return index;
}

/*
 * determine the textarea of the current page
 */
int textarea(struct position *position, struct output *output) {
	void (*order[])(RectangleList *, PopplerPage *) = {
		rectanglelist_quicksort,
		rectanglelist_twosort,
		rectanglelist_charsort
	};
	double overlap, frag;

	if (! POPPLER_IS_PAGE(position->page)) {
		*output->reload = TRUE;
		return -1;
	}

	rectanglelist_free(position->textarea);
	poppler_rectangle_free(position->boundingbox);

	switch (output->viewmode) {
	case 0:
	case 1:
		position->textarea =
			rectanglelist_textarea_distance(position->page,
				output->distance);
		if (position->textarea->num == 0) {
			rectanglelist_free(position->textarea);
			position->textarea = NULL;
			position->boundingbox = NULL;
			break;
		}
		position->boundingbox =
			rectanglelist_joinall(position->textarea);
		overlap = interoverlap(position);
		frag = fragmented(position);
		// ensureoutputfile(output);
		// fprintf(output->outfile, "auto: %g %g\n", overlap, frag);
		if (output->viewmode == 0 && (overlap < 0.8 || frag > 1.0)) {
			rectanglelist_free(position->textarea);
			position->textarea = NULL;
			break;
		}
		order[output->order](position->textarea, position->page);
		break;
	case 2:
#if POPPLER_CHECK_VERSION(0, 90, 0)
		position->boundingbox = poppler_rectangle_new();
		poppler_page_get_bounding_box(position->page,
			position->boundingbox);
#else
		position->boundingbox =
			rectanglelist_boundingbox(position->page);
#endif
		position->textarea = NULL;
		break;
	case 3:
		position->boundingbox = pagerectangle(position->page);
		position->textarea = NULL;
		break;
	}
	if (position->boundingbox == NULL)
		position->boundingbox = pagerectangle(position->page);
	if (position->textarea == NULL) {
		position->textarea = rectanglelist_new(1);
		rectanglelist_add(position->textarea, position->boundingbox);
	}

	return 0;
}

/*
 * read the current page
 */
int readpage(struct position *position, struct output *output) {
	readpageraw(position, output);
	textarea(position, output);
	return 0;
}

/*
 * translate from textbox coordinates to screen coordinates and back
 */
double xdoctoscreen(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_user_to_device(output->cr, &xx, &yy);
	return xx;
}
double xscreentodoc(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_device_to_user(output->cr, &xx, &yy);
	return xx;
}
double ydoctoscreen(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_user_to_device(output->cr, &xx, &yy);
	return yy;
}
double yscreentodoc(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_device_to_user(output->cr, &xx, &yy);
	return yy;
}

/*
 * translate distances from screen coordinates to textbox coordinates
 */
double xscreentodocdistance(struct output *output, double x) {
	double xx = x, yy = 0.0;
	cairo_device_to_user_distance(output->cr, &xx, &yy);
	return xx;
}
double yscreentodocdistance(struct output *output, double y) {
	double xx = 0.0, yy = y;
	cairo_device_to_user_distance(output->cr, &xx, &yy);
	return yy;
}

/*
 * translate a rectangle from textbox coordindates to screen and back
 */
void rscreentodoc(struct output *output,
		PopplerRectangle *dst, PopplerRectangle *src) {
	dst->x1 = xscreentodoc(output, src->x1);
	dst->y1 = yscreentodoc(output, src->y1);
	dst->x2 = xscreentodoc(output, src->x2);
	dst->y2 = yscreentodoc(output, src->y2);
}

/*
 * size of destination rectangle translated to document coordinates
 */
double xdestsizetodoc(struct output *output) {
	return xscreentodocdistance(output, output->dest.x2 - output->dest.x1);
}
double ydestsizetodoc(struct output *output) {
	return yscreentodocdistance(output, output->dest.y2 - output->dest.y1);
}

/*
 * change scrollx and scrolly to avoid space outside boundingbox being shown
 */
int adjustscroll(struct position *position, struct output *output) {

	/* some space at the right of the bounding box is shown */
	if (xdoctoscreen(output, position->boundingbox->x2 - position->scrollx)
	    < output->dest.x2)
		position->scrollx = position->boundingbox->x2 -
			xscreentodoc(output, output->dest.x2);

	/* some space at the left of the bounding box is shown */
	if (xdoctoscreen(output, position->boundingbox->x1 - position->scrollx)
	    > output->dest.x1)
		position->scrollx = position->boundingbox->x1 -
			xscreentodoc(output, output->dest.x1);

	/* bounding box too narrow to fill the screen */
	if (position->boundingbox->x2 - position->boundingbox->x1 <
	    xscreentodocdistance(output, output->dest.x2 - output->dest.x1))
		position->scrollx =
			(position->boundingbox->x1 +
			 position->boundingbox->x2) / 2 -
			xscreentodoc(output,
				(output->dest.x1 + output->dest.x2) / 2);

	/* some space below the bounding box is shown */
	if (ydoctoscreen(output, position->boundingbox->y2 - position->scrolly)
	    < output->dest.y2)
		position->scrolly = position->boundingbox->y2 -
			yscreentodoc(output, output->dest.y2);

	/* some space over the bounding box is shown */
	if (ydoctoscreen(output, position->boundingbox->y1 - position->scrolly)
	    > output->dest.y1)
		position->scrolly = position->boundingbox->y1 -
			yscreentodoc(output, output->dest.y1);

	/* bounding box too short to fill the screen */
	if (position->boundingbox->y2 - position->boundingbox->y1 <
	    yscreentodocdistance(output, output->dest.y2 - output->dest.y1))
		position->scrolly =
			(position->boundingbox->y1 +
			 position->boundingbox->y2) / 2 -
			yscreentodoc(output,
				(output->dest.y1 + output->dest.y2) / 2);

	return 0;
}

/*
 * adjust width of viewbox to the minimum allowed
 */
void adjustviewbox(struct position *position, struct output *output) {
	double d, minwidth, minheight;
	PopplerRectangle *viewbox;
	int fitmode;

	fitmode = output->fit;
	viewbox = position->viewbox;
	minwidth = xscreentodocdistance(output, output->minwidth *
		(output->dest.x2 - output->dest.x1) / output->screenwidth);
	minheight = yscreentodocdistance(output, output->minwidth *
		(output->dest.y2 - output->dest.y1) / output->screenheight);

	if (fitmode == 0 ||
	    (fitmode & 0x1 && viewbox->x2 - viewbox->x1 < minwidth)) {
		d = minwidth - viewbox->x2 + viewbox->x1;
		viewbox->x1 -= d / 2;
		viewbox->x2 += d / 2;
	}

	if (fitmode == 0 ||
	    (fitmode & 0x2 && viewbox->y2 - viewbox->y1 < minheight)) {
		d = minheight - viewbox->y2 + viewbox->y1;
		viewbox->y1 -= d / 2;
		viewbox->y2 += d / 2;
	}
}

/*
 * move to position
 */
void moveto(struct position *position, struct output *output) {
	PopplerRectangle scaled;

	cairo_identity_matrix(output->cr);

	/* scale to match the screen aspect; also scale the destination
	 * rectangle in the opposite way, so that position->viewbox is still
	 * mapped to output->dest in spite of the scaling */
	scaled = output->dest;
	if (output->fit & 0x1) {
		cairo_scale(output->cr, 1.0, output->aspect);
		scaled.y1 = scaled.y1 / output->aspect;
		scaled.y2 = scaled.y2 / output->aspect;
	}
	else {
		cairo_scale(output->cr, 1.0 / output->aspect, 1.0);
		scaled.x1 = scaled.x1 * output->aspect;
		scaled.x2 = scaled.x2 * output->aspect;
	}

	poppler_rectangle_free(position->viewbox);
	position->viewbox = poppler_rectangle_copy
		(&position->textarea->rect[position->box]);
	adjustviewbox(position, output);
	rectangle_map_to_cairo(output->cr, &scaled, position->viewbox,
		output->fit == 1, output->fit == 2, TRUE, TRUE, TRUE);

	adjustscroll(position, output);
	cairo_translate(output->cr, -position->scrollx, -position->scrolly);
}

/*
 * go to the top of current textbox
 */
int toptextbox(struct position *position, struct output *output) {
	(void) output;
	position->scrollx = 0;
	position->scrolly = 0;
	moveto(position, output);

	/* scrolling moves the origin of the viewbox; scrolling to zero places
	 * the top left corner of the viewbox at the top left corner of the
	 * screen; this is usually correct, as it makes the textbox centered on
	 * the screen (the textboxes at the border are then repositioned by
	 * adjustscroll() to avoid empty space being shown); but when fit=none,
	 * the viewbox may be smaller than the textbox; scrolling to zero would
	 * show the middle of the textbox instead of its upper left corner */
	position->scrollx = MIN(0,
		position->textarea->rect[position->box].x1 -
		position->viewbox->x1);
	position->scrolly = MIN(0,
		position->textarea->rect[position->box].y1 -
		position->viewbox->y1);
	return 0;
}

/*
 * go to the top of the first textbox of the page
 */
int firsttextbox(struct position *position, struct output *output) {
	position->box = 0;
	return toptextbox(position, output);
}

/*
 * move to the start of the next page
 */
int nextpage(struct position *position, struct output *output) {
	if (position->npage + 1 >= position->totpages)
		return -1;

	position->npage++;
	readpage(position, output);
	return firsttextbox(position, output);
}

/*
 * move to the top of the next textbox
 */
int nexttextbox(struct position *position, struct output *output) {
	if (position->box + 1 >= position->textarea->num)
		return output->fit == 0 ? 0 : nextpage(position, output);

	position->box++;
	return toptextbox(position, output);
}

/*
 * scroll down
 */
int scrolldown(struct position *position, struct output *output) {
	moveto(position, output);
	/* bottom of textbox already within the destination rectangle */
	if (ydoctoscreen(output, position->textarea->rect[position->box].y2) <=
	    output->dest.y2 + 0.3)
		return nexttextbox(position, output);

	position->scrolly += yscreentodocdistance(output,
		(output->dest.y2 - output->dest.y1) * output->scroll);
	return 0;
}

/*
 * scroll right
 */
int scrollright(struct position *position, struct output *output) {
	moveto(position, output);
	/* right edge of textbox already within the destination rectangle */
	if (xdoctoscreen(output, position->textarea->rect[position->box].x2) <=
	    output->dest.x2 + 0.3)
		return nexttextbox(position, output);

	position->scrollx += xscreentodocdistance(output,
		(output->dest.x2 - output->dest.x1) * output->scroll);
	return 0;
}

/*
 * move to the bottom/right of the current textbox
 */
int bottomtextbox(struct position *position, struct output *output) {
	position->scrollx = 0;
	position->scrolly = 0;
	moveto(position, output);

	/* the viewbox is scrolled so that its bottom right corner is at the
	 * bottom right corner of the screen; this makes the textbox
	 * horizontally centered on the screen since the textbox is in the
	 * center of the viewbox (the textboxes at the border of the page are
	 * then repositioned by adjustscroll() to avoid empty space being
	 * shown); but when fit=none the viewbox may be smaller than the
	 * textbox, and the screen would show the middle of the textbox instead
	 * of its bottom right corner; in this case, the scrolling has to place
	 * at the bottom right corner of the screen the corner of the textbox
	 * instead of the viewbox */
	position->scrollx =
		MAX(position->viewbox->x2,
			position->textarea->rect[position->box].x2)
		- xscreentodoc(output, output->dest.x2);
	position->scrolly =
		MAX(position->viewbox->y2,
			position->textarea->rect[position->box].y2)
		- yscreentodoc(output, output->dest.y2);
	return 0;
}

/*
 * go to the bottom of the last textbox in the page
 */
int lasttextbox(struct position *position, struct output *output) {
	position->box = position->textarea->num - 1;
	return bottomtextbox(position, output);
}

/*
 * move to the bottom of the previous page
 */
int prevpage(struct position *position, struct output *output) {
	if (position->npage <= 0)
		return -1;

	position->npage--;
	readpage(position, output);
	return lasttextbox(position, output);
}

/*
 * move to the bottom of previous textbox
 */
int prevtextbox(struct position *position, struct output *output) {
	if (position->box - 1 < 0)
		return output->fit == 0 ? 0 : prevpage(position, output);

	position->box--;
	return bottomtextbox(position, output);
}

/*
 * scroll up
 */
int scrollup(struct position *position, struct output *output) {
	moveto(position, output);
	/* top of current textbox already within the destination rectangle */
	if (ydoctoscreen(output, position->textarea->rect[position->box].y1) >=
	    output->dest.y1 - 0.3)
		return prevtextbox(position, output);

	position->scrolly -= yscreentodocdistance(output,
		(output->dest.y2 - output->dest.y1) * output->scroll);
	return 0;
}

/*
 * scroll left
 */
int scrollleft(struct position *position, struct output *output) {
	moveto(position, output);
	/* left edge of the textbox already within the destination rectangle */
	if (xdoctoscreen(output, position->textarea->rect[position->box].x1) >=
	    output->dest.x1 - 0.3)
		return prevtextbox(position, output);

	position->scrollx -= xscreentodocdistance(output,
		(output->dest.x2 - output->dest.x1) * output->scroll);
	return 0;
}

/*
 * check whether a rectangle is before/after the top/bottom of the screen
 */
int relativescreen(struct output *output, PopplerRectangle *r,
		gboolean inscreen, gboolean after) {
	double x, y;

	if (after) {
		x = xdoctoscreen(output, r->x1);
		y = ydoctoscreen(output, r->y1);
		if (inscreen)
			return x >= output->dest.x1 && y >= output->dest.y1;
		else
			return x > output->dest.x2 || y > output->dest.y2;
	}
	else {
		x = xdoctoscreen(output, r->x2);
		y = ydoctoscreen(output, r->y2);
		if (inscreen)
			return x <= output->dest.x2 && y <= output->dest.y2;
		else
			return x < output->dest.x1 || y < output->dest.y1;
	}
}

/*
 * position a rectangle in the current textbox at the top or bottom of screen
 */
int scrolltorectangle(struct position *position, struct output *output,
		PopplerRectangle *r, gboolean top, gboolean bottom) {
	PopplerRectangle *t;

	t = &position->textarea->rect[position->box];
	toptextbox(position, output);
	moveto(position, output);
	if (output->fit != 1)
		position->scrollx =
			top ?
				r->x1 - t->x1 - 40 :
			bottom ?
				r->x2 - t->x1 + 40 - xdestsizetodoc(output) :
			/* center */
				(r->x1 + r->x2) / 2 + t->x1 -
					xdestsizetodoc(output) / 2;
	if (output->fit != 2)
		position->scrolly =
			top ?
				r->y1 - t->y1 - 40 :
			bottom ?
				r->y2 - t->y1 + 40 - ydestsizetodoc(output) :
			/* center */
				(r->y1 + r->y2) / 2 - t->y1 -
					ydestsizetodoc(output) / 2;
	return adjustscroll(position, output);
}

/*
 * go to the first or next selected rectangle in the page, if any
 */
int movetoselected(struct position *position, struct output *output,
		GList *selection, int forward, int *current,
		gboolean beforescreen, gboolean inscreen,
		gboolean afterscreen) {
	int b;
	int end, step;
	double width, height, y1;
	PopplerRectangle *t, r, s;
	GList *o, *l;
	int previous, i, firstfound;

	if (selection == NULL)
		return -1;

	end = forward ? position->textarea->num : -1;
	step = forward ? +1 : -1;

	if (forward) {
		o = selection;
		previous = *current;
	}
	else {
		o = g_list_copy(selection);
		o = g_list_reverse(o);
		previous = g_list_length(o) - *current - 1;
	}

	poppler_page_get_size(position->page, &width, &height);
	for (b = position->box; b != end; b += step) {
		t = &position->textarea->rect[b];
		firstfound = -1;
		for (l = o, i = 0; l != NULL; l = l->next, i++) {
			r = * (PopplerRectangle *) l->data;
			y1 = r.y1;
			r.y1 = height - r.y2;
			r.y2 = height - y1;

			if (! rectangle_contain(t, &r))
				continue;

			if (! afterscreen &&
			    relativescreen(output, &r, FALSE, forward))
				continue;

			if (! beforescreen &&
			    ! relativescreen(output, &r, inscreen, forward))
				continue;

			if (! beforescreen && i == previous &&
			    relativescreen(output, &r, TRUE, ! forward)) {
				firstfound = -1;
				continue;
			}

			if (firstfound == -1) {
				firstfound = i;
				s = r;
				if (i > previous)
					break;
			}
		}
		if (firstfound != -1) {
			position->box = b;
			if (! relativescreen(output, &s, TRUE, ! forward) ||
			    ! relativescreen(output, &s, TRUE, forward))
				scrolltorectangle(position, output, &s,
			                  forward, ! forward);

			if (forward)
				setcurrent(current, firstfound);
			else {
				setcurrent(current,
					g_list_length(o) - firstfound - 1);
				g_list_free(o);
			}
			return 0;
		}
		if (! afterscreen)
			break;
		inscreen = TRUE;
		beforescreen = TRUE;
	}

	if (! forward)
		g_list_free(o);
	return -1;
}

/*
 * scan the document using a temporary position
 */
void positionscan(struct position *position, struct position *scan, int step) {
	RectangleList *rl;
	PopplerRectangle *bb, *v;

	if (step == 0) {		// init
		*scan = *position;
		g_object_ref(scan->page);
		scan->textarea = rectanglelist_copy(position->textarea);
		scan->boundingbox = poppler_rectangle_copy(scan->boundingbox);
		scan->viewbox = NULL; // do not free, is also in *position
		return;
	}

	if (step == -1) {		// finish
		rectanglelist_free(scan->textarea);
		poppler_rectangle_free(scan->boundingbox);
		poppler_rectangle_free(scan->viewbox);
		return;
	}

					// move to
	rl = position->textarea;
	bb = position->boundingbox;
	v = position->viewbox;
	scan->permanent_id = position->permanent_id;
	scan->update_id = position->update_id;
	g_set_object(&position->page, scan->page);
	g_object_unref(scan->page);
	*position = *scan;
	scan->textarea = rl;
	scan->boundingbox = bb;
	scan->viewbox = v;
}

/*
 * go to the first/next match in the document
 */
int gotomatch(struct position *position, struct output *output,
		int step, int firstsearch) {
	static struct position scan;

	if (output->search[0] == '\0')
		return -2;

	if (step == 0) {		// init
		positionscan(position, &scan, 0);
		moveto(&scan, output);
		pagematch(&scan, output);
		if (firstsearch)
			setcurrent(&output->current, CURRENT_NONE);
	}

	if (step == -1) {		// finish
		positionscan(position, &scan, -1);
		return -1;
	}

	if (scan.page == NULL) {
		readpageraw(&scan, output);
		if (output->found != NULL) {
			textarea(&scan, output);
			if (output->forward)
				firsttextbox(&scan, output);
			else
				lasttextbox(&scan, output);
			moveto(&scan, output);
		}
	}

	if (! movetoselected(&scan, output,
			output->found, output->forward, &output->current,
			step != 0,
			firstsearch || output->current != CURRENT_UNUSED,
			TRUE)) {
		positionscan(position, &scan, 1);
		return -1;
	}
			
	g_set_object(&scan.page, NULL);
	scan.npage = (scan.npage + (output->forward ? 1 : -1) + scan.totpages)
		% scan.totpages;
	return scan.npage;
}

/*
 * move to a given page
 */
int movetopage(struct cairoui *cairoui, int page) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	if (page < 0 || page >= position->totpages) {
		cairoui_printlabel(cairoui, output->help,
			2000, "no such page: %d", pagepdftoui(output, page));
		return -1;
	}
	if (page == position->npage)
		return -2;
	position->npage = page;
	readpage(position, output);
	return firsttextbox(position, output);
}

/*
 * move to a named destination
 */
int movetonameddestination(struct cairoui *cairoui, char *name) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	PopplerDest *dest;
	double width, height;
	PopplerRectangle r, *s, *p;
	int pos;

	dest = poppler_document_find_dest(position->doc, name);
	if (dest == NULL) {
		cairoui_printlabel(cairoui, output->help,
			2000, "no such destination: %s", name);
		return -1;
	}

	if (position->npage != dest->page_num - 1) {
		position->npage = dest->page_num - 1;
		readpage(position, output);
	}

	poppler_page_get_size(position->page, &width, &height);
	r.x1 = dest->change_left ? dest->left : 0.0;
	r.y1 = dest->change_top ? height - dest->top : height;
	r.x2 = r.x1 + 1;
	r.y2 = r.y1 + 1;

	pos = rectanglelist_contain(position->textarea, &r);
	if (pos == -1)
		pos = rectanglelist_overlap(position->textarea, &r);
	if (pos == -1) {
		r.x1 = 0.0;
		r.x2 = width;
		pos = rectanglelist_overlap(position->textarea, &r);
	}
	if (pos != -1)
		position->box = pos;
	scrolltorectangle(position, output, &r, TRUE, FALSE);

	p = &position->textarea->rect[position->box];
	s = poppler_rectangle_new();
	s->x1 = dest->change_left ? dest->left : p->x1;
	s->y1 = dest->change_top ? dest->top - output->texfudge : p->y1;
	s->x2 = s->x1 + 12;
	s->y2 = s->y1 + 12;
	freeglistrectangles(output->selection);
	output->selection = g_list_append(NULL, s);

	poppler_dest_free(dest);

	return 0;
}

/*
 * check whether the bounding box is all in the screen
 */
int boundingboxinscreen(struct position *position, struct output *output) {
	if (position->boundingbox->x2 - position->boundingbox->x1 >
	    xdestsizetodoc(output))
		return FALSE;
	if (position->boundingbox->y2 - position->boundingbox->y1 >
	    ydestsizetodoc(output))
		return FALSE;
	return TRUE;
}

/*
 * first non-existing file matching a pattern containing %d
 */
FILE *firstfree(char *pattern, int *number) {
	int fd;
	char path[PATH_MAX];
	FILE *out;

	for (*number = 1; *number < 1000; (*number)++) {
		snprintf(path, PATH_MAX, pattern, *number);
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd == -1)
			continue;
		out = fdopen(fd, "w");
		return out;
	}
	return NULL;
}

/*
 * save a range of pages or a rectangle within to file
 */
cairo_status_t writetofile(void *c, const unsigned char *d, unsigned int l) {
	int res;
	res = fwrite(d, l, 1, (FILE *) c);
	return res == 1 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}
int savepdf(int c, struct cairoui *cairoui,
		int first, int last, PopplerRectangle *r, gboolean samepage,
		gboolean post) {
	static FILE *out;
	static cairo_surface_t *surface;
	static cairo_t *cr;
	static int fileno;
	static int npage;
	static cairo_status_t status;

	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	int tot;
	PopplerPage *page;
	gdouble width, height;
	char path[PATH_MAX];
	char *command;
	int f;

	tot = poppler_document_get_n_pages(position->doc);
	if (first < 0 || last > tot - 1 || last < first) {
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"invalid page range");
		return CAIROUI_LEAVE;
	}

	if (c == KEY_INIT) {
		out = firstfree(output->pdfout, &fileno);
		if (out == NULL) {
			cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
				"failed opening output file");
			return CAIROUI_FAIL;
		}
		surface = cairo_pdf_surface_create_for_stream(writetofile,
				out, 1, 1);
		cr = cairo_create(surface);

		cairoui_printlabel(cairoui, output->help, 0, "saving...");
		cairoui->redraw = TRUE;

		npage = first;
		cairoui->timeout = 0;
		return CAIROUI_CHANGED;
	}

	if (c == '\033')
		return CAIROUI_LEAVE;

	if (c != KEY_FINISH) {
		page = poppler_document_get_page(position->doc, npage);
		if (page == NULL) {
			status = CAIRO_STATUS_READ_ERROR;
			return CAIROUI_FAIL;
		}
		poppler_page_get_size(page, &width, &height);
		cairo_identity_matrix(cr);
		if (r == NULL || samepage)
			cairo_pdf_surface_set_size(surface, width, height);
		else {
			cairo_pdf_surface_set_size(surface,
				r->x2 - r->x1 + 10, r->y2 - r->y1 + 10);
			cairo_translate(cr, -r->x1 + 5, -r->y1 + 5);
		}
		if (r != NULL) {
			cairo_rectangle(cr, r->x1, r->y1,
				r->x2 - r->x1, r->y2 - r->y1);
			cairo_clip(cr);
		}
		poppler_page_render_for_printing(page, cr);
		cairo_surface_show_page(surface);
		status = cairo_surface_status(surface);
		g_object_unref(page);
		cairoui->timeout = 0;
		if (status != CAIRO_STATUS_SUCCESS)
			return CAIROUI_FAIL;
		cairoui_printlabel(cairoui, output->help, 0,
			"    saved page %-5d ", pagepdftoui(output, npage));
		npage++;
		return npage > last ? CAIROUI_DONE : CAIROUI_CHANGED;
	}

	if (out == NULL)
		return CAIROUI_FAIL;
	cairo_destroy(cr);
	cairo_surface_finish(surface);
	if (status == CAIRO_STATUS_SUCCESS)
		status = cairo_surface_status(surface);
	cairo_surface_destroy(surface);
	f = fclose(out);
	sprintf(path, output->pdfout, fileno);
	if (status != CAIRO_STATUS_SUCCESS || f != 0) {
		unlink(path);
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"error saving file");
		// do not print npage in the help label, error may have
		// occurred much earlier
		return CAIROUI_FAIL;
	}
	else if (npage <= last) {
		unlink(path);
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"save canceled");
		return CAIROUI_LEAVE;
	}
	else {
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"saved to %s", path);
		if (post && output->postsave != NULL) {
			command =
				malloc(strlen(output->postsave) + 100);
			sprintf(command, output->postsave,
					fileno, fileno);
			system(command);
			free(command);
		}
		return CAIROUI_DONE;
	}
}

/*
 * save the current textbox to file
 */
int savecurrenttextbox(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	PopplerRectangle screen, sdoc, save;
	int o;

	screen = output->dest;
	rectangle_expand(&screen, 10, 10);
	moveto(position, output);
	rscreentodoc(output, &sdoc, &screen);
	rectangle_intersect(&save,
		&sdoc, &position->textarea->rect[position->box]);

	o = savepdf(KEY_INIT, cairoui,
			position->npage, position->npage, &save, false, true);
	while (! CAIROUI_OUT(o))
		o = savepdf(KEY_NONE, cairoui,
			position->npage, position->npage, &save, false, true);
	return savepdf(KEY_FINISH, cairoui,
			position->npage, position->npage, &save, false, true);
}

/*
 * convert a cairo rectangle to a poppler rectangle
 */
void cairorectangletopoppler(PopplerRectangle *p, cairo_rectangle_t *c) {
	p->x1 = c->x;
	p->y1 = c->y;
	p->x2 = c->x + c->width;
	p->y2 = c->y + c->height;
}

/*
 * append the coordindates of a box to a file
 */
int savebox(struct cairoui *cairoui, PopplerRectangle *r) {
	struct output *output = OUTPUT(cairoui);
	char line[70];
	char *result;

	snprintf(line, 70, "%g %g %g %g", r->x1, r->y1, r->x2, r->y2);

	if (ensureoutputfile(cairoui))
		result = "- error opening output file";
	else {
		rectangle_print(cairoui->outfile, r);
		fputs("\n", cairoui->outfile);
		fflush(cairoui->outfile);
		result = "- saved to";
	}

	cairoui_printlabel(cairoui, output->help,
		2000, "%s %s %s", line, result, cairoui->outname);
	return 0;
}

/*
 * append the coordinates of the current textbox to a file
 */
int savecurrentbox(struct cairoui *cairoui, int visible) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	PopplerRectangle r, sdoc;

	if (! visible)
		r = position->textarea->rect[position->box];
	else {
		rscreentodoc(output, &sdoc, &output->dest);
		rectangle_intersect(&r, &sdoc,
			&position->textarea->rect[position->box]);
	}

	return savebox(cairoui, &r);
}

/*
 * find or scan the external script key string
 */
int findentry(char *config, char f, char *key, char **entry) {
	char *s;
	int menu, underline, found, next;

	if (f == ' ' && entry == NULL)
		return 0;

	menu = 0;
	underline = 0;
	next = 0;
	found = 0;
	*key = ' ';
	if (entry != NULL)
		*entry = NULL;
	for(s = config; *s != '\0'; s++) {
		if (menu && next && entry != NULL && *entry == NULL)
			*entry = s;
		if (*s == '[')
			menu = 1;
		if (menu) {
			if (*s == ']')
				menu = 0;
			continue;
		}
		if (*s == f || f == ' ') {
			if (*key == ' ' || (f == ' ' && *entry == NULL))
				*key = *s;
			next = 1;
		}
		if (! underline) {
			if (*s == f || f == ' ')
				found = 1;
			else
				next = 0;
		}
		underline = *s == '_';
	}

	return found;
}

/*
 * call the external script
 */
int keyscript(struct cairoui *cairoui, char c, gboolean unescaped) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	char key;
	PopplerRectangle s, d;
	char *line, out[100], textbox[200], dest[200], rectangle[200];
	int len, res;
	FILE *pipe;

	if (output->script == NULL || output->keys == NULL)
		return -1;

	res = findentry(output->keys, c, &key, NULL);
	if ((! res && unescaped) || key == ' ')
		return -1;

	d = position->textarea->rect[position->box];
	sprintf(textbox, "[%g,%g-%g,%g]", d.x1, d.y1, d.x2, d.y2);
	s = output->dest;
	rscreentodoc(output, &d, &s);
	sprintf(dest, "[%g,%g-%g,%g]", d.x1, d.y1, d.x2, d.y2);
	if (output->rectangle == NULL)
		sprintf(rectangle, "[]");
	else {
		cairorectangletopoppler(&s, output->rectangle);
		moveto(position, output);
		rscreentodoc(output, &d, &s);
		sprintf(rectangle, "[%g,%g-%g,%g]", d.x1, d.y1, d.x2, d.y2);
	}

	len = strlen(output->script) + strlen(position->filename) + 620;
	line = malloc(len);
	sprintf(line, "%s %c \"%s\" %d %d %s %s %s",
	        output->script, c,
		position->filename,
		position->npage + 1, position->totpages,
		textbox, dest, rectangle);
	pipe = popen(line, "r");
	if (pipe == NULL)
		return -1;
	res = fread(out, 1, 80, pipe);
	pclose(pipe);
	if (res < 0)
		cairoui_printlabel(cairoui, output->help, 2000,
			"executed: %s", line);
	else {
		out[res] = '\0';
		cairoui_printlabel(cairoui, output->help, 2000, out);
	}
	free(line);
	return res;
}

/*
 * the windows
 */
enum window {
	WINDOW_DOCUMENT,
	WINDOW_HELP,
	WINDOW_TUTORIAL,
	WINDOW_GOTOPAGE,
	WINDOW_SEARCH,
	WINDOW_NEXT,
	WINDOW_CHOP,
	WINDOW_VIEWMODE,
	WINDOW_FITDIRECTION,
	WINDOW_ORDER,
	WINDOW_SCRIPT,
	WINDOW_RECTANGLE,
	WINDOW_MENU,
	WINDOW_WIDTH,
	WINDOW_DISTANCE
};

/*
 * document window
 */
int document(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static gboolean labels;

	switch (c) {
	case 'r':
		cairoui->reload = TRUE;
		cairoui->redraw = TRUE;
		break;
	case KEY_INIT:
	case KEY_TIMEOUT:
	case KEY_RESIZE:
	case KEY_FINISH:
		return WINDOW_DOCUMENT;
	case KEY_REFRESH:
		labels = FALSE;
		cairoui->flush = TRUE;
		return WINDOW_DOCUMENT;
	case 'q':
		return CAIROUI_EXIT;
	case KEY_HELP:
	case 'h':
		return output->ui ? WINDOW_HELP : WINDOW_DOCUMENT;
	case KEY_OPTIONS:
	case 'm':
		return output->ui ? WINDOW_MENU : WINDOW_DOCUMENT;
	case KEY_MOVE:
	case 'g':
		return WINDOW_GOTOPAGE;
	case 'c':
		return WINDOW_CHOP;
	case 'C':
		savecurrenttextbox(cairoui);
		break;
	case 'w':
		return WINDOW_WIDTH;
	case 't':
		return WINDOW_DISTANCE;
	case 'o':
		return WINDOW_ORDER;
	case 'e':
		return WINDOW_SCRIPT;
	case 'd':
		return WINDOW_RECTANGLE;
	case KEY_FIND:
	case '/':
	case '?':
		output->forward = c != '?';
		return WINDOW_SEARCH;
	case 'n':
	case 'p':
		output->forward = c == 'n';
		return WINDOW_NEXT;
	case ' ':
		if (output->fit == 0 || output->fit == 1)
			scrolldown(position, output);
		else if (output->fit == 2)
			scrollright(position, output);
		else
			nexttextbox(position, output);
		break;
	case KEY_DOWN:
		scrolldown(position, output);
		break;
	case KEY_UP:
		scrollup(position, output);
		break;
	case KEY_LEFT:
		scrollleft(position, output);
		break;
	case KEY_RIGHT:
		scrollright(position, output);
		break;
	case KEY_HOME:
		firsttextbox(position, output);
		break;
	case KEY_END:
		lasttextbox(position, output);
		break;
	case KEY_NPAGE:
		nextpage(position, output);
		break;
	case KEY_PPAGE:
		prevpage(position, output);
		break;
	case 'v':
		output->viewmode = (output->viewmode + 1) % 4;
		firsttextbox(position, output);
		readpage(position, output);
		break;
	case 'z':
		if (output->minwidth <= 1)
			break;
		output->minwidth -= output->minwidth - 10 < 10 ? 1 : 10;
		if (output->fit & 0x1)
			position->scrollx = 0;
		if (output->fit & 0x2)
			position->scrolly = 0;
		break;
	case 'Z':
		if (boundingboxinscreen(position, output))
			break;
		output->minwidth += output->minwidth < 10 ? 1 : 10;
		break;
	case 'f':
		output->fit = (output->fit + 1) % 4;
		position->scrollx = 0;
		position->scrolly = 0;
		break;
	case 'a':
		cairoui->usearea = ! cairoui->usearea;
		cairoui_reset(cairoui);
		break;
	case 's':
		if (labels) {
			cairoui->flush = TRUE;
			labels = FALSE;
			return WINDOW_DOCUMENT;
		}
		labels = TRUE;
		cairoui->timeout = 3000;
		output->pagenumber = TRUE;
		output->showmode = TRUE;
		output->showfit = TRUE;
		output->filename = TRUE;
		break;
	case 'b':
	case 'B':
		savecurrentbox(cairoui, c == 'B');
		break;
	case '\\':	/* for testing */
		movetonameddestination(cairoui, "abcd");
		break;
	default:
		keyscript(cairoui, c, TRUE);
	}

	cairoui->redraw = TRUE;
	cairoui->flush = TRUE;
	return WINDOW_DOCUMENT;
}

/*
 * help
 */
int help(int c, struct cairoui *cairoui) {
	static char *helptext[] = {
		HOVACUI " - pdf viewer with autozoom to text",
		"PageUp     previous page",
		"PageDown   next page",
		"Home       top of page",
		"End        bottom of page",
		"m          main menu",
		"v          change view mode:",
		"           textarea, boundingbox, page",
		"f          change fitting direction:",
		"           horizontal, vertical, both",
		"w z Z      minimal width: set, -, +",
		"           (determines the maximal zoom)",
		"t          text-to-text distance",
		"o          order of the blocks of text",
		"g          go to page",
		"/ ?        search forward or backward",
		"n p        next or previous search match",
		"s          show current mode and page",
		"b          show and save current box",
		"r          reload the current document",
		"h          help",
		"q          quit",
		"",
		"any key to continue",
		NULL
	};
	static int line = 0;

	return cairoui_list(c, cairoui, helptext, &line, NULL) ==
		CAIROUI_LEAVE ? WINDOW_DOCUMENT : WINDOW_HELP;
}

/*
 * tutorial
 */
int tutorial(int c, struct cairoui *cairoui) {
	struct output *output = OUTPUT(cairoui);
	static char *tutorialtext[] = {
		HOVACUI " - pdf viewer with autozoom to text",
		HOVACUI " displays a block of text at time",
		"the current block is bordered in blue",
		"",
		"zoom is automatic",
		"move by cursor%s%s" " and PageUp/PageDown",
		"",
		"key h for help",
		"key m for menu",
		"key v for whole page view",
		"",
		"space bar to view document",
		NULL
	};
	static char cursor[100];
	static int line = 0;
	int i;

	for (i = 0; tutorialtext[i] != NULL; i++)
		if (strstr(tutorialtext[i], "%s")) {
			sprintf(cursor, tutorialtext[i],
				output->fit == 1 ? " Up/Down" : "",
				output->fit == 2 ? " Left/Right" : "");
			tutorialtext[i] = cursor;
		}

	return c == 'h' ? WINDOW_HELP :
		cairoui_list(c, cairoui, tutorialtext, &line, NULL)
			== CAIROUI_LEAVE ? WINDOW_DOCUMENT : WINDOW_TUTORIAL;
}

/*
 * chop menu, to save a range of pages
 */
int chop(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char *choptext[] = {
		"save",
		"first page of range",
		"last page of range",
		"save pages in range",
		"clear range",
		"",
		"save document",
		"save current page",
		"save current box",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	static gboolean iterating = FALSE;
	static int res;
	int o;
	int first, last;

	if (iterating) {
	}
	else {
		if (c == KEY_FINISH)
			return WINDOW_DOCUMENT;
		else if (output->first != -1 && output->last != -1)
			cairoui_printlabel(cairoui, output->help,
				NO_TIMEOUT, "range: %d-%d",
				pagepdftoui(output, output->first),
				pagepdftoui(output, output->last));
		else if (output->first != -1)
			cairoui_printlabel(cairoui, output->help,
				NO_TIMEOUT, "range: %d-",
				pagepdftoui(output, output->first));
		else if (output->last != -1)
			cairoui_printlabel(cairoui, output->help,
				NO_TIMEOUT, "range: -%d",
				pagepdftoui(output, output->last));
		else
			output->help[0] = '\0';

		if (c == KEY_INIT)
			selected = output->first == -1 ? 1 :
			           output->last == -1 ? 2 : 3;

		res = cairoui_list(c, cairoui, choptext, &line, &selected);
	}

	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_CHOP;
	switch (selected) {
	case 1:
		output->first = position->npage;
		output->help[0] = '\0';
		break;
	case 2:
		output->last = position->npage;
		output->help[0] = '\0';
		break;
	case 3:
		first = output->first == -1 ?
				output->last == -1 ? position->npage : 0 :
				output->first;
		last = output->last == -1 ?
				output->first == -1 ?
					position->npage :
					position->totpages - 1 :
				output->last;
		if (! iterating) {
			iterating = TRUE;
			c = KEY_INIT;
		}
		o = savepdf(c, cairoui, first, last, NULL, true, false);
		if (! CAIROUI_OUT(o))
			return WINDOW_CHOP;
		if (o == CAIROUI_DONE) {
			output->first = -1;
			output->last = -1;
		}
		break;
	case 4:
		output->first = -1;
		output->last = -1;
		output->help[0] = '\0';
		break;
	case 6:
		first = 0;
		last = position->totpages - 1;
		if (! iterating) {
			iterating = TRUE;
			c = KEY_INIT;
		}
		o = savepdf(c, cairoui, first, last, NULL, true, false);
		if (! CAIROUI_OUT(o))
			return WINDOW_CHOP;
		break;
	case 7:
		first = position->npage;
		last = position->npage;
		if (! iterating) {
			iterating = TRUE;
			c = KEY_INIT;
		}
		o = savepdf(c, cairoui, first, last, NULL, true, true);
		if (! CAIROUI_OUT(o))
			return WINDOW_CHOP;
		break;
	case 8:
		savecurrenttextbox(cairoui);
		break;
	}

	if (c == KEY_FINISH)
		iterating = FALSE;
	return WINDOW_DOCUMENT;
}

/*
 * viewmode menu
 */
int viewmode(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char *viewmodetext[] = {
		"view mode",
		"auto",
		"text area",
		"boundingbox",
		"page",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	if (c == KEY_INIT)
		selected = output->viewmode + 1;

	res = cairoui_list(c, cairoui, viewmodetext, &line, &selected);
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_VIEWMODE;
	switch (selected) {
	case 1:
	case 2:
	case 3:
	case 4:
		output->viewmode = selected - 1;
		textarea(position, output);
		firsttextbox(position, output);
		if (output->immediate)
			return CAIROUI_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * fit direction menu
 */
int fitdirection(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char *fitdirectiontext[] = {
		"fit direction",
		"none",
		"horizontal",
		"vertical",
		"both",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	if (c == KEY_INIT)
		selected = output->fit + 1;

	res = cairoui_list(c, cairoui, fitdirectiontext, &line, &selected);
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_FITDIRECTION;
	switch (selected) {
	case 1:
	case 2:
	case 3:
	case 4:
		output->fit = selected - 1;
		firsttextbox(position, output);
		if (output->immediate)
			return CAIROUI_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * sorting algorithm menu
 */
int order(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char *fitdirectiontext[] = {
		"block ordering algorithm",
		"quick",
		"two-step",
		"char",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;
	(void) position;

	if (c == KEY_INIT)
		selected = output->order + 1;

	res = cairoui_list(c, cairoui, fitdirectiontext, &line, &selected);
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_ORDER;
	switch (selected) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		output->order = selected - 1;
		textarea(position, output);
		firsttextbox(position, output);
		if (output->immediate)
			return CAIROUI_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * build a menu out of a config string (max 99 entries)
 */
char **entrymenu(char *config, char **keys) {
	char **menu;
	int max = 100;
	char *s, *end;
	int i;
	char key;
	char *entry;

	menu = malloc(max * sizeof(char *));
	if (keys != NULL)
		*keys = malloc(max * sizeof(char));

	for (s = config, i = 1;
             s != NULL && i < max;
             s = strchr(s, ']'), i++) {
		if (s != config)
			s++;
		findentry(s, ' ', &key, &entry);
		if (entry == NULL)
			break;
		if (keys != NULL)
			(*keys)[i] = key;
		end = strchr(entry, ']');
		menu[i] = malloc((end - entry + 1) * sizeof(char));
		memcpy(menu[i], entry, end - entry);
		menu[i][end - entry] = '\0';
	}
	menu[i] = NULL;

	return menu;
}

/*
 * menu for the external script
 */
int script(int c, struct cairoui *cairoui) {
	struct output *output = OUTPUT(cairoui);

	static char **menu;
	static char *keys;
	static int line = 0;
	static int selected = 1;
	int i;
	int res;

	if (c == KEY_INIT) {
		menu = entrymenu(output->keys, &keys);
		if (menu[1] == NULL) {
			cairoui_printlabel(cairoui, output->help, 2000,
				"no external script; "
				"see man page for details");
			return WINDOW_DOCUMENT;
		}
		menu[0] = "external script";
	}

	if (c == KEY_FINISH) {
		for (i = 1; menu[i] != NULL; i++)
			free(menu[i]);
		free(menu);
		free(keys);
		return WINDOW_DOCUMENT;
	}

	res = cairoui_list(c, cairoui, menu, &line, &selected);
	if (! CAIROUI_OUT(res))
		return WINDOW_SCRIPT;

	if (res == CAIROUI_DONE)
		keyscript(cairoui, keys[selected], FALSE);

	return WINDOW_DOCUMENT;

}

/*
 * main menu
 */
int menu(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char *menutext[] = {
		HOVACUI " - menu",
		"(g) go to page",
		"(/) search",
		"(c) save document or page selection",
		"(d) draw a rectangle",
		"(e) external script",
		"(v) view mode",
		"(f) fit direction",
		"(w) minimal width",
		"(t) text distance",
		"(o) block order",
		"(h) help",
		"(q) quit",
		NULL
	};
	static char *shortcuts = "g/cdevfwtohq", *s;
	static int menunext[] = {
		WINDOW_MENU,
		WINDOW_GOTOPAGE,
		WINDOW_SEARCH,
		WINDOW_CHOP,
		WINDOW_RECTANGLE,
		WINDOW_SCRIPT,
		WINDOW_VIEWMODE,
		WINDOW_FITDIRECTION,
		WINDOW_WIDTH,
		WINDOW_DISTANCE,
		WINDOW_ORDER,
		WINDOW_HELP,
		CAIROUI_EXIT,
		-1,
	};
	static int line = 0;
	static int selected = 1;
	int n, res;
	(void) position;

	for (n = 0; menunext[n] != -1; n++) {
	}

	if (c == KEY_INIT)
		selected = 1;

	s = strchr(shortcuts, c);

	if (s == NULL)
		res = cairoui_list(c, cairoui, menutext, &line, &selected);
	else {
		res = CAIROUI_DONE;
		selected = s - shortcuts + 1;
	}

	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_MENU;

	if (selected >= 0 && selected < n)
		return menunext[selected];
	if (selected >= n)
		cairoui_printlabel(cairoui, output->help,
			2000, "unimplemented");
	return WINDOW_DOCUMENT;
}

/*
 * field for a search keyword
 */
int search(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);

	static char searchstring[100] = "", prevstring[100] = "";
	static char *outcome = NULL;
	static gboolean iterating = FALSE;
	static int res;
	static int pos = 0;
	static int nsearched;

	char *prompt = "find: ";
	int page;

	if (iterating) {
		if (c == KEY_REFRESH)
			cairoui_field(KEY_REFRESH, cairoui, prompt,
				searchstring, &pos, outcome);
	}
	else {
		if (c == KEY_INIT)
			nsearched = 0;

		if (c == KEY_FINISH) {
			strcpy(output->search, "");
			pagematch(position, output);
			strcpy(prevstring, searchstring);
			searchstring[0] = '\0';
			pos = 0;
			outcome = NULL;
			return WINDOW_DOCUMENT;
		}

		if (ISREALKEY(c))
			outcome = NULL;

		if (c == KEY_UP) {
			strcpy(searchstring, prevstring);
			pos = strlen(searchstring);
			c = KEY_NONE;
		}

		res = cairoui_field(c, cairoui, prompt, searchstring, &pos,
			outcome);
	}

	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;

	if (res != CAIROUI_DONE)
		return WINDOW_SEARCH;

	if (! iterating) {
		strcpy(output->search, searchstring);
		if (searchstring[0] == '\0') {
			pagematch(position, output);
			return WINDOW_DOCUMENT;
		}
		outcome = "searching";
		nsearched = 0;
		iterating = TRUE;
	}

	if (c == KEY_FINISH) {
		gotomatch(position, output, -1, FALSE);
		strcpy(prevstring, searchstring);
		searchstring[0] = '\0';
		pos = 0;
		iterating = FALSE;
		outcome = NULL;
		return WINDOW_DOCUMENT;
	}

	if (c == KEY_EXIT || c == '\033' || c == 's' || c == 'q') {
		gotomatch(position, output, -1, FALSE);
		outcome = "stopped";
		iterating = FALSE;
		return CAIROUI_REFRESH;
	}

	page = gotomatch(position, output, nsearched, nsearched == 0);
	if (page == -1) {
		cairoui_printlabel(cairoui, output->help,
			2000, "n=next matches p=previous matches");
		return WINDOW_DOCUMENT;
	}

	nsearched++;
	if (nsearched <= position->totpages) {
		cairoui_printlabel(cairoui, output->help,
			0, "    searching page %-5d ", page + 1);
		return nsearched == 1 ? CAIROUI_REFRESH : WINDOW_SEARCH;
	}

	cairoui->redraw = 1;
	cairoui_printlabel(cairoui, output->help, 0, "");
	gotomatch(position, output, -1, FALSE);
	outcome = "no match";
	iterating = FALSE;
	return WINDOW_SEARCH;
}

/*
 * next match of a search
 */
int next(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static int nsearched = 0;
	int page;

	if (c == KEY_INIT)
		nsearched = 0;

	if (c == KEY_FINISH) {
		gotomatch(position, output, -1, FALSE);
		pagematch(position, output);
		return WINDOW_DOCUMENT;
	}

	if (c == KEY_EXIT || c == '\033' || c == 's' || c == 'q')
		return WINDOW_DOCUMENT;

	page = gotomatch(position, output, nsearched, FALSE);
	if (page == -1) {
		cairoui_printlabel(cairoui, output->help,
			2000, "n=next matches p=previous matches");
		return WINDOW_DOCUMENT;
	}
	else if (page == -2) {
		cairoui_printlabel(cairoui, output->help,
			2000, "no previous search");
		return WINDOW_DOCUMENT;
	}

	nsearched++;
	if (nsearched <= position->totpages) {
		cairoui_printlabel(cairoui, output->help,
			0, "    searching \"%s\" on page %-5d ",
			output->search, page + 1);
		return WINDOW_NEXT;
	}
	cairoui_printlabel(cairoui, output->help,
		2000, "not found: %s\n", output->search);
	return WINDOW_DOCUMENT;
}

/*
 * field for a page number
 */
int gotopage(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char gotopagestring[100] = "";
	static int pos = 0;
	int res;
	int n;

	switch (c) {
	case KEY_INIT:
		c = KEY_REFRESH;
		break;
	case KEY_PPAGE:
	case KEY_NPAGE:
		c = c == KEY_PPAGE ? KEY_UP : KEY_DOWN;
		break;
	case 'c':
		sprintf(gotopagestring, "%d",
			pagepdftoui(output, position->npage));
		c = KEY_REFRESH;
		break;
	case 'f':
		sprintf(gotopagestring, "%d", pagepdftoui(output, 0));
		c = KEY_REFRESH;
		break;
	case 'l':
		sprintf(gotopagestring, "%d",
			pagepdftoui(output, position->totpages - 1));
		c = KEY_REFRESH;
		break;
	}

	n = pagepdftoui(output, position->npage);
	res = cairoui_number(c, cairoui,
		"go to page: ", gotopagestring, &pos, NULL,
		&n,
		pagepdftoui(output, 0),
		pagepdftoui(output, position->totpages - 1));
	switch (res) {
	case CAIROUI_DONE:
		if (position->npage != pageuitopdf(output, n)) {
			position->npage = pageuitopdf(output, n);
			readpage(position, output);
			firsttextbox(position, output);
		}
		if (output->immediate)
			return CAIROUI_REFRESH;
		/* fallthrough */
	case CAIROUI_LEAVE:
		gotopagestring[0] = '\0';
		pos = 0;
		output->help[0] = '\0';
		return WINDOW_DOCUMENT;
	case CAIROUI_UNCHANGED:
		return WINDOW_GOTOPAGE;
	case CAIROUI_INVALID:
		cairoui_number(KEY_REFRESH, cairoui,
			"go to page: ", gotopagestring, &pos, "no such page",
			&n,
			pagepdftoui(output, 0),
			pagepdftoui(output, position->totpages - 1));
		/* fallthrough */
	default:
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"c=current f=first l=last up=-1 down=+1 enter=go");
		return WINDOW_GOTOPAGE;
	}
}

/*
 * field for the minimal width
 */
int minwidth(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char minwidthstring[100] = "";
	static int pos = 0;
	int res;

	res = cairoui_number(c, cairoui,
		"minimal width: ", minwidthstring, &pos, NULL,
		&output->minwidth, 0, 1000);
	if (res == CAIROUI_DONE) {
		readpage(position, output);
		firsttextbox(position, output);
		return output->immediate ? CAIROUI_REFRESH : WINDOW_DOCUMENT;
	}
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	cairoui_printlabel(cairoui, output->help,
		NO_TIMEOUT, "down=increase up=decrease");
	return WINDOW_WIDTH;
}

/*
 * field for the text distance
 */
int textdistance(int c, struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static char distancestring[100] = "";
	static int pos = 0;
	int res;

	res = cairoui_number(c, cairoui,
		"text distance: ", distancestring, &pos, NULL,
		&output->distance, 0, 1000);
	if (res == CAIROUI_DONE) {
		readpage(position, output);
		firsttextbox(position, output);
		return output->immediate ? CAIROUI_REFRESH : WINDOW_DOCUMENT;
	}
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	cairoui_printlabel(cairoui, output->help,
		NO_TIMEOUT, "down=increase up=decrease");
	return WINDOW_DISTANCE;
}

/*
 * rectangle drawing by cursor keys
 */
int rectangle(int c, struct cairoui *cairoui) {
	static cairo_rectangle_t r;
	static gboolean corner;
	static gboolean iterating = FALSE;
	static int res, savec;
	static PopplerRectangle d;
	static int first, last;
	static gboolean showhelp = TRUE;

	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	PopplerRectangle s;
	int currc;
	int o;

	if (iterating) {
		currc = c;
		c = savec;
	}
	else {
		if (c == KEY_INIT) {
			r = cairoui->dest;
			corner = FALSE;
			output->rectangle = &r;
		}
		if (c == 'c' || c == 'd')
			corner = ! corner;

		res = cairoui_rectangle(c, cairoui, corner, &r);
	}
	if (c == KEY_FINISH)
		output->rectangle = NULL;

	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res == CAIROUI_DONE || c == 's' || c == 'S') {
		if (! iterating) {
			cairorectangletopoppler(&s, &r);
			moveto(position, output);
			rscreentodoc(output, &d, &s);
			savebox(cairoui, &d);
			if (res == CAIROUI_DONE)
				return WINDOW_DOCUMENT;
			first = c == 'S' ? 0 : position->npage;
			last = c == 'S' ?
				position->totpages - 1 : position->npage;
			iterating = TRUE;
			savec = c;
			currc = KEY_INIT;
		}
		o = savepdf(currc, cairoui, first, last, &d,
			c == 'S', c != 'S');
		if (! CAIROUI_OUT(o))
			return WINDOW_RECTANGLE;
		if (currc == KEY_FINISH)
			iterating = FALSE;
		return WINDOW_DOCUMENT;
	}
	if (res == CAIROUI_REFRESH || c == 'd')
		return CAIROUI_REFRESH;
	if (res == CAIROUI_UNCHANGED && -1 != keyscript(cairoui, c, TRUE)) {
		showhelp = FALSE;
		return CAIROUI_REFRESH;
	}
	if (res == CAIROUI_UNCHANGED && cairoui->redraw)
		return CAIROUI_REFRESH;

	if (showhelp)
		cairoui_printlabel(cairoui, output->help, NO_TIMEOUT,
			"c/d=opposite corner, enter=save, s/S=save content");
	showhelp = TRUE;
	return WINDOW_RECTANGLE;
}

struct windowlist windowlist[] = {
{	WINDOW_DOCUMENT,	"DOCUMENT",	document	},
{	WINDOW_HELP,		"HELP",		help		},
{	WINDOW_TUTORIAL,	"TUTORIAL",	tutorial	},
{	WINDOW_GOTOPAGE,	"GOTOPAGE",	gotopage	},
{	WINDOW_SEARCH,		"SEARCH",	search		},
{	WINDOW_NEXT,		"NEXT",		next		},
{	WINDOW_CHOP,		"CHOP",		chop		},
{	WINDOW_VIEWMODE,	"VIEWMODE",	viewmode	},
{	WINDOW_FITDIRECTION,	"FITDIRECTION",	fitdirection	},
{	WINDOW_ORDER,		"ORDER",	order		},
{	WINDOW_SCRIPT,		"SCRIPT",	script		},
{	WINDOW_RECTANGLE,	"RECTANGLE",	rectangle	},
{	WINDOW_MENU,		"MENU",		menu		},
{	WINDOW_WIDTH,		"WIDTH",	minwidth	},
{	WINDOW_DISTANCE,	"DISTANCE",	textdistance	},
{	0,			NULL,		NULL		}
};

/*
 * show some help
 */
void helplabel(struct cairoui *cairoui) {
	struct output *output = OUTPUT(cairoui);

	if (output->help[0] == '\0')
		return;

	cairoui_label(cairoui, output->help, 1);
	output->help[0] = '\0';
}

/*
 * check whether the current page contains annotations that are not links
 */
int checkannotations(struct position *position) {
	GList *annots, *s;
	int present = FALSE;
	PopplerAnnotMapping *m;

	if (! POPPLER_IS_PAGE(position->page))
		return FALSE;

	annots = poppler_page_get_annot_mapping(position->page);

	for (s = annots; s != NULL && ! present; s = s->next) {
		m = (PopplerAnnotMapping *) s->data;
		switch (poppler_annot_get_annot_type(m->annot)) {
		case POPPLER_ANNOT_LINK:
			break;
		default:
			present = TRUE;
		}
	}

	poppler_page_free_annot_mapping(annots);
	return present;
}

/*
 * check whether the current page contains actions that are not internal links
 */
int checkactions(struct position *position) {
	GList *actions, *s;
	int present = FALSE;
	PopplerLinkMapping *m;

	if (! POPPLER_IS_PAGE(position->page))
		return FALSE;

	actions = poppler_page_get_link_mapping(position->page);

	for (s = actions; s != NULL && ! present; s = s->next) {
		m = (PopplerLinkMapping *) s->data;
		switch (m->action->type) {
		case POPPLER_ACTION_GOTO_DEST:
		case POPPLER_ACTION_NAMED:
			break;
		default:
			present = TRUE;
		}
	}

	poppler_page_free_link_mapping(actions);
	return present;
}

/*
 * show the current page number
 */
void pagenumber(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static int prev = -1;
	char r[30], s[100];
	int hasannots, hasactions;
	char *other, *annots, *actions, clock[9];
	time_t t;
	struct tm n;

	if ((position->npage == prev || ! output->pagelabel) &&
	    ! output->pagenumber)
		return;

	if (output->first != -1 && output->last != -1)
		snprintf(r, 30, " - range:%d-%d",
			pagepdftoui(output, output->first),
			pagepdftoui(output, output->last));
	else if (output->first != -1)
		snprintf(r, 30, " - range:%d-",
			pagepdftoui(output, output->first));
	else if (output->last != -1)
		snprintf(r, 30, " - range:-%d",
			pagepdftoui(output, output->last));
	else
		r[0] = '\0';

	hasannots = checkannotations(position);
	hasactions = checkactions(position);
	other = hasannots || hasactions ? " - contains" : "";
	annots = hasannots ? " annotations" : "";
	actions = hasactions ? hasannots ? " and actions" : " actions" : "";


	t = time(NULL);

	if (! output->showclock)
		clock[0] = '\0';
	else {
		localtime_r(&t, &n);
		strftime(clock, 9, " - %H:%M", &n);
	}

	if (output->totalpages && output->offset == 1)
		snprintf(s, 100, "page %d of %d%s%s%s%s%s",
			pagepdftoui(output, position->npage),
			pagepdftoui(output, position->totpages - 1),
			other, annots, actions, r, clock);
	else if (output->totalpages) {
		snprintf(s, 100, "page %d in %d-%d%s%s%s%s%s",
			pagepdftoui(output, position->npage),
			pagepdftoui(output, 0),
			pagepdftoui(output, position->totpages - 1),
			other, annots, actions, r, clock);
	}
	else
		sprintf(s, "page %d%s%s%s%s%s",
			pagepdftoui(output, position->npage),
			other, annots, actions, r, clock);
	cairoui_label(cairoui, s, 2);

	if (cairoui->timeout == NO_TIMEOUT)
		cairoui->timeout = 1200;
	output->pagenumber = FALSE;
	prev = position->npage;
}

/*
 * show the current mode
 */
void showmode(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static int prev = -1;
	char s[100];
	char *modes[] = {"auto", "textarea", "boundingbox", "page"};
	char *actual;

	(void) position;

	if (output->viewmode == prev && ! output->showmode)
		return;

	actual = output->viewmode != 0 ? "" :
		position->textarea == NULL || position->textarea->num == 1 ?
			" (boundingbox)" : " (textarea)";
	sprintf(s, "viewmode: %s%s", modes[output->viewmode], actual);
	cairoui_label(cairoui, s, 3);

	if (cairoui->timeout == NO_TIMEOUT)
		cairoui->timeout = 1200;
	output->showmode = FALSE;
	prev = output->viewmode;
}

/*
 * show the current fit direction
 */
void showfit(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	static int prev = -1;
	char s[100];
	char *fits[] = {"none", "horizontal", "vertical", "both"};

	(void) position;

	if (output->fit == prev && ! output->showfit)
		return;

	sprintf(s, "fit: %s", fits[output->fit]);
	cairoui_label(cairoui, s, 4);

	if (cairoui->timeout == NO_TIMEOUT)
		cairoui->timeout = 1200;
	output->showfit = FALSE;
	prev = output->fit;
}

/*
 * show the filename
 */
void filename(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	char s[100];

	if (! output->filename)
		return;

	snprintf(s, 100, "%s", position->filename);
	cairoui_label(cairoui, s, 5);

	if (cairoui->timeout == NO_TIMEOUT)
		cairoui->timeout = 1200;
	output->filename = FALSE;
}

/*
 * list of labels
 */
void (*labellist[])(struct cairoui *) = {
	helplabel,
	pagenumber,
	showmode,
	showfit,
	filename,
	NULL
};

/*
 * draw a list of rectangles in pdf points
 */
void selection(struct cairoui *cairoui, GList *s, int current) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	double width, height;
	GList *l;
	PopplerRectangle *r;
	int i;

	cairo_save(output->cr);
	cairo_scale(output->cr, 1, -1);
	poppler_page_get_size(position->page, &width, &height);
	cairo_translate(output->cr, 0, -height);
	cairo_set_operator(output->cr, CAIRO_OPERATOR_DIFFERENCE);
	for (l = s, i = 0; l != NULL; l = l->next, i++) {
		if (current == i)
			cairo_set_source_rgb(output->cr, 0.0, 0.3, 0.3);
		else
			cairo_set_source_rgb(output->cr, 0.3, 0.0, 0.3);
		r = (PopplerRectangle *) l->data;
		cairo_rectangle(output->cr,
			r->x1, r->y1, r->x2 - r->x1, r->y2 - r->y1);
		cairo_fill(output->cr);
	}
	cairo_stroke(output->cr);
	cairo_restore(output->cr);
}

/*
 * check whether the pdf file was changed; only works after rendering a page
 */
int changedpdf(struct position *position) {
	gchar *permanent_id, *update_id;
	int res;

	permanent_id = position->permanent_id;
	update_id = position->update_id;

	poppler_document_get_id(position->doc,
		&position->permanent_id, &position->update_id);

	res = update_id == NULL ? FALSE :
		! ! memcmp(update_id, position->update_id, 32);

	g_free(permanent_id);
	g_free(update_id);
	return res;
}

/*
 * draw the page border
 */
void pageborder(struct position *position, struct output *output) {
	double width, height;
	poppler_page_get_size(position->page, &width, &height);
	cairo_set_source_rgb(output->cr, 1.0, 0.8, 0.8);
	cairo_rectangle(output->cr, 0, 0, width, height);
	cairo_stroke(output->cr);
}

/*
 * draw the document
 */
void draw(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);

	moveto(position, output);
	if (! POPPLER_IS_PAGE(position->page)) {
		cairoui->reload = TRUE;
		cairoui->redraw = TRUE;
		return;
	}
	cairoui_logstatus(LEVEL_DRAW, NULL, 0, cairoui, KEY_NONE);
	poppler_page_render(position->page, output->cr);
	if (changedpdf(position)) {
		cairoui->reload = TRUE;
		cairoui->redraw = TRUE;
		return;
	}
	if (output->drawbox) {
		rectangle_draw(output->cr,
			&position->textarea->rect[position->box],
			FALSE, FALSE, TRUE);
		pageborder(position, output);
	}
	selection(cairoui, output->found, output->current);
	selection(cairoui, output->selection, -1);
}

/*
 * resize output
 */
void resize(struct cairoui *cairoui) {
	struct cairodevice *cairodevice = cairoui->cairodevice;
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);

	output->cr = cairoui->cr;

	output->dest.x1 = cairoui->dest.x;
	output->dest.y1 = cairoui->dest.y;
	output->dest.x2 = cairoui->dest.x + cairoui->dest.width;
	output->dest.y2 = cairoui->dest.y + cairoui->dest.height;

	output->screenwidth = cairodevice->screenwidth(cairodevice);
	output->screenheight = cairodevice->screenheight(cairodevice);
	output->aspect = output->screenaspect == -1 ?
		1 : output->screenaspect * output->screenheight /
		output->screenwidth;

	/* undo box centering */
	if (output->fit & 0x1)
		position->scrollx = 0;
	if (output->fit & 0x2)
		position->scrolly = 0;
}

/*
 * open a pdf file
 */
struct position *openpdf(char *filename) {
	char *uri;
	struct position *position;
	GError *err;

	position = malloc(sizeof(struct position));

	position->filename = strdup(filename);

	uri = filenametouri(position->filename);
	if (uri == NULL)
		return NULL;
	err = NULL;
	position->doc = poppler_document_new_from_file(uri, NULL, &err);
	free(uri);
	if (position->doc == NULL) {
		printf("error opening %s: %s\n", filename, err->message);
		free(position);
		return NULL;
	}

	position->totpages = poppler_document_get_n_pages(position->doc);
	if (position->totpages < 1) {
		printf("no page in document\n");
		free(position);
		return NULL;
	}

	poppler_document_get_id(position->doc,
		&position->permanent_id, &position->update_id);

	position->page = NULL;

	return position;
}

/*
 * reload a pdf file
 */
void reloadpdf(struct cairoui *cairoui) {
	struct position *position = POSITION(cairoui);
	struct output *output = OUTPUT(cairoui);
	struct position *new;

	new = openpdf(position->filename);
	if (new == NULL)
		return;
	initposition(new);

	if (position->npage >= new->totpages) {
		new->npage = new->totpages - 1;
		readpage(new, output);
		lasttextbox(new, output);
		free(position);
		((struct callback *) cairoui->cb)->position = new;
		return;
	}
	new->npage = position->npage;
	readpage(new, output);

	if (position->box >= new->textarea->num)
		new->box = new->textarea->num - 1;
	else {
		new->box = position->box;
		new->scrollx = position->scrollx;
		new->scrolly = position->scrolly;
	}
	free(position);
	((struct callback *) cairoui->cb)->position = new;
}

/*
 * close a pdf file
 */
void closepdf(struct position *position) {
	free(position->filename);
	free(position);
}

/*
 * execute an external command
 */
int external(struct cairoui* cairoui, int window) {
	struct output *output = OUTPUT(cairoui);
	struct command *command = &cairoui->command;
	char *newline;
	int page;
	char dest[100];

	newline = strchr(command->command, '\n');
	if (newline)
		*newline = '\0';

	if (command->command[0] == '#' || ! strcmp(command->command, "nop"))
		return window;
	if (! strcmp(command->command, "quit"))
		return CAIROUI_EXIT;
	if (! strcmp(command->command, "document"))
		return WINDOW_DOCUMENT;
	if (! strcmp(command->command, "reload")) {
		cairoui->reload = TRUE;
		return CAIROUI_REFRESH;
	}
	if (1 == sscanf(command->command, "gotopage %d", &page)) {
		return movetopage(cairoui, page) ?
			window : CAIROUI_REFRESH;
	}
	if (1 == sscanf(command->command, "gotodestination %90s", dest)) {
		return movetonameddestination(cairoui, dest) ?
			window : CAIROUI_REFRESH;
	}
	if (1 == sscanf(command->command, "offset %d", &output->offset)) {
		cairoui_printlabel(cairoui, output->help,
			4000, "new page 1 set");
		return CAIROUI_REFRESH;
	}

	cairoui_printlabel(cairoui, output->help,
		4000, "error in command: %s", command->command);
	return window;
}

/*
 * open the input fifo for external commands
 */
int openfifo(char *name, struct command *command, int *keepopen) {
	if (command->stream != NULL)
		fclose(command->stream);
	else if (command->fd != -1)
		close(command->fd);
	if (*keepopen != -1)
		close(*keepopen);

	command->fd = open(name, O_RDONLY | O_NONBLOCK);
	if (command->fd == -1) {
		perror(name);
		return -1;
	}
	*keepopen = open(name, O_WRONLY);
	command->stream = fdopen(command->fd, "r");
	return 0;
}

/*
 * index of a character in a string
 */
int optindex(char arg, char *all) {
	char *pos;
	pos = index(all, arg);
	if (pos == NULL)
		return -1;
	return pos - all;
}

/*
 * scan a fraction like w:h or n/d
 */
double fraction(char *arg) {
	char *col;
	col = index(arg, ':');
	if (col == NULL) {
		col = index(arg, '/');
		if (col == NULL)
			return atof(arg);
	}
	*col = '\0';
	return atof(arg) / atof(col + 1);
}

/*
 * usage
 */
void usage(char *additional) {
	printf("pdf viewer with automatic zoom to text\n");
	printf("usage:\n\t" HOVACUI "\t[-m viewmode] [-f direction] ");
	printf("[-w minwidth] [-t distance] [-p]\n");
	printf("\t\t[-s aspect] [-d device] file.pdf\n");
	printf("\t\t-m viewmode\tzoom to: text, boundingbox, page\n");
	printf("\t\t-f direction\tfit: horizontally, vertically, both\n");
	printf("\t\t-w minwidth\tminimal width (maximal zoom)\n");
	printf("\t\t-t distance\tminimal text distance\n");
	printf("\t\t-p\t\tpresentation mode\n");
	printf("\t\t-s aspect\tthe screen aspect (e.g., 4:3)\n");
	printf("\t\t-d device\tfbdev device, default /dev/fb0\n");
	printf("\t\t-e fifo\t\treceive commands from the given fifo\n");
	printf("\t\t-z out\t\toutput file or fifo\n");
	printf("\t\t-l level\tlogging level\n");
	puts(additional);
	printf("main keys: 'h'=help 'g'=go to page '/'=search 'q'=quit ");
	printf("'m'=menu\n");
}

/*
 * show a pdf file on an arbitrary cairo device
 */
int hovacui(int argn, char *argv[], struct cairodevice *cairodevice) {
	char *mainopts = "m:f:w:t:o:d:s:pO:F:e:z:l:c:C:h", *allopts;
	char configfile[4096], configline[1000], s[1000], r[1000];
	FILE *config;
	int i, n;
	double d;
	char *outdev;
	int canopen;
	char *filename;
	struct cairoui cairoui;
	struct callback callback;
	struct output output;
	int opt;
	char *res;
	int doublebuffering;
	int firstwindow;
	int noinitlabels;
	int keepopen;

				/* defaults */

	cairoui_default(&cairoui);

	cairoui.cairodevice = cairodevice;
	callback.output = &output;
	cairoui.cb = &callback;

	cairoui.draw = draw;
	cairoui.resize = resize;
	cairoui.update = reloadpdf;
	cairoui.external = external;
	cairoui.windowlist = windowlist;
	cairoui.labellist = labellist;

	cairoui.outname = "hovacui-out.txt";
	cairoui.margin = 10.0;
	cairoui.fontsize = -1;

	output.viewmode = 0;
	output.totalpages = FALSE;
	output.showclock = FALSE;
	output.fit = 1;
	output.minwidth = -1;
	output.distance = -1;
	output.order = 1;
	output.scroll = 1.0 / 4.0;
	output.offset = 1;
	output.ui = TRUE;
	output.immediate = FALSE;
	output.reload = &cairoui.reload;
	output.drawbox = TRUE;
	output.pagelabel = TRUE;
	output.current = CURRENT_UNUSED;
	output.pdfout = "selection-%d.pdf";
	output.postsave = NULL;
	output.keys = NULL;
	output.script = NULL;
	output.first = -1;
	output.last = -1;
	output.screenaspect = -1;
	output.rectangle = NULL;

	firstwindow = WINDOW_TUTORIAL;
	outdev = NULL;
	noinitlabels = FALSE;
	keepopen = -1;
	doublebuffering = TRUE;

				/* merge commandline options */

	allopts = malloc(strlen(mainopts) + strlen(cairodevice->options) + 1);
	strcpy(allopts, mainopts);
	strcat(allopts, cairodevice->options);

				/* location of the config file */

	snprintf(configfile, 4096, "%s/.config/hovacui/hovacui.conf",
		getenv("HOME"));
	optind = 1;
	while (-1 != (opt = getopt(argn, argv, allopts)))
		if (opt == 'C') {
			snprintf(configfile, 4096, "%s", optarg);
			break;
		}

				/* config file lines and -c options */

	optind = 1;
	config = fopen(configfile, "r");
	res = NULL;
	while ((config != NULL && (res = fgets(configline, 900, config))) ||
	       (-1 != (opt = getopt(argn, argv, allopts)))) {
		if (res == NULL && opt != 'c')
			continue;
		if (res == NULL) {
			strncpy(configline, optarg, 900);
			res = strchr(configline, '=');
			if (res != NULL)
				*res = ' ';
		}

		if (configline[0] == '#')
			continue;

		if (sscanf(configline, "mode %s", s) == 1)
			output.viewmode = optindex(s[0], "atbp");
		if (sscanf(configline, "fit %s", s) == 1)
			output.fit = optindex(s[0], "nhvb");
		if (sscanf(configline, "minwidth %lg", &d) == 1)
			output.minwidth = d;
		if (sscanf(configline, "order %s", s) == 1)
			output.order = optindex(s[0], "qtc");
		if (sscanf(configline, "distance %lg", &d) == 1)
			output.distance = d;
		if (sscanf(configline, "aspect %s", s) == 1)
			output.screenaspect = fraction(s);
		if (sscanf(configline, "scroll %s", s) == 1)
			output.scroll = fraction(s);
		if (sscanf(configline, "fontsize %lg", &d) == 1)
			cairoui.fontsize = d;
		if (sscanf(configline, "margin %lg", &d) == 1)
			cairoui.margin = d;
		n = sscanf(configline, "area [%lg,%lg,%lg,%lg]",
			&cairoui.area.x, &cairoui.area.y,
			&cairoui.area.width, &cairoui.area.height);
		if (0 < n && n < 4) {
			cairoui.area.width = -1;
			cairoui.area.height = -1;
		}
		if (sscanf(configline, "device %s", s) == 1)
			outdev = strdup(s);
		if (sscanf(configline, "fifo %s", s) == 1)
			if (openfifo(s, &cairoui.command, &keepopen))
				exit(EXIT_FAILURE);
		if (sscanf(configline, "outfile %s", s) == 1)
			cairoui.outname = strdup(s);
		if (sscanf(configline, "postsave %900[^\n\r]", s) == 1)
			output.postsave = strdup(s);
		if (sscanf(configline, "script %900s \"%900[^\"]\"",
		           s, r) == 2 ||
		    sscanf(configline, "script %900s '%900[^']'", s, r) == 2 ||
		    sscanf(configline, "script %900s %900s", s, r) == 2) {
			output.keys = strdup(r);
			output.script = strdup(s);
		}
		if (sscanf(configline, "log %d", &i) == 1)
			cairoui.log = i;

		if (sscanf(configline, "%s", s) == 1) {
			if (! strcmp(s, "noui"))
				output.ui = FALSE;
			if (! strcmp(s, "immediate"))
				output.immediate = TRUE;
			if (! strcmp(s, "nobox"))
				output.drawbox = FALSE;
			if (! strcmp(s, "nopagelabel"))
				output.pagelabel = FALSE;
			if (! strcmp(s, "notutorial"))
				firstwindow = WINDOW_DOCUMENT;
			if (! strcmp(s, "totalpages"))
				output.totalpages = TRUE;
			if (! strcmp(s, "clock"))
				output.showclock = TRUE;
			if (! strcmp(s, "noinitlabels"))
				noinitlabels = TRUE;
			if (! strcmp(s, "presentation")) {
				output.viewmode = optindex('p', "atbp");
				output.fit = optindex('b', "nhvb");
				output.ui = FALSE;
				output.drawbox = FALSE;
				output.pagelabel = FALSE;
				output.totalpages = TRUE;
				cairoui.margin = 0.0;
				firstwindow = WINDOW_DOCUMENT;
				noinitlabels = TRUE;
			}
			if (! strcmp(s, "doublebuffering"))
				doublebuffering = 1;
			if (! strcmp(s, "nodoublebuffering"))
				doublebuffering = 0;
			if (! strcmp(s, "navigatematches"))
				output.current = CURRENT_NONE;
		}
	}
	if (config != NULL)
		fclose(config);

				/* environment variables */

	if (getenv("DOUBLEBUFFERING") != NULL)
		doublebuffering = ! ! strcmp(getenv("DOUBLEBUFFERING"), "no");

				/* cmdline arguments */

	optind = 1;
	while (-1 != (opt = getopt(argn, argv, allopts)))
		switch (opt) {
		case 'm':
			output.viewmode = optindex(optarg[0], "atbp");
			if (output.viewmode == -1) {
				printf("unsupported mode: %s\n", optarg);
				usage(cairodevice->usage);
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			output.fit = optindex(optarg[0], "nhvb");
			if (output.fit == -1) {
				printf("unsupported fit mode: %s\n", optarg);
				usage(cairodevice->usage);
				exit(EXIT_FAILURE);
			}
			break;
		case 'w':
			output.minwidth = atof(optarg);
			if (output.minwidth < 0) {
				printf("error: negative minimal width\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			output.distance = atof(optarg);
			if (output.distance < 0) {
				printf("error: negative text distance\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'o':
			output.order = optindex(optarg[0], "qtc");
			if (output.order == -1) {
				printf("unsupported ordering: %s\n", optarg);
				usage(cairodevice->usage);
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			output.viewmode = optindex('p', "atbp");
			output.fit = optindex('b', "nhvb");
			output.ui = FALSE;
			output.drawbox = FALSE;
			output.pagelabel = FALSE;
			output.totalpages = TRUE;
			cairoui.margin = 0.0;
			firstwindow = WINDOW_DOCUMENT;
			noinitlabels = TRUE;
			break;
		case 'd':
			outdev = optarg;
			break;
		case 's':
			output.screenaspect = fraction(optarg);
			break;
		case 'O':
			output.offset = atoi(optarg);
			break;
		case 'F':
			output.offset = -atoi(optarg) + 2;
			break;
		case 'e':
			if (openfifo(optarg, &cairoui.command, &keepopen))
				exit(EXIT_FAILURE);
			break;
		case 'z':
			cairoui.outname = optarg;
			break;
		case 'l':
			cairoui.log = atoi(optarg);
			break;
		case 'h':
			usage(cairodevice->usage);
			exit(EXIT_SUCCESS);
			break;
		}

	if (argn - 1 < optind) {
		printf("file name missing\n");
		usage(cairodevice->usage);
		exit(EXIT_FAILURE);
	}
	filename = argv[optind];

				/* open input file */

	callback.position = openpdf(filename);
	if (callback.position == NULL)
		exit(EXIT_FAILURE);
	initposition(callback.position);
	initpage(callback.position, pageuitopdf(&output, 1));

				/* open output device as cairo */

	canopen = cairodevice->init(cairodevice,
		outdev, doublebuffering, argn, argv, allopts);
	if (canopen == -1) {
		cairodevice->finish(cairodevice);
		exit(EXIT_FAILURE);
	}
	free(allopts);

				/* initialize output */

	strcpy(output.search, "");
	output.found = NULL;
	output.selection = NULL;
	output.texfudge = 0; // 24;
	output.help[0] = '\0';
	output.help[79] = '\0';
	if (output.minwidth == -1)
		output.minwidth = 400;

	if (noinitlabels)
		cairoui_initlabels(&cairoui);
	else
		cairoui_printlabel(&cairoui, output.help,
			2000, "press 'h' for help");

	readpage(callback.position, &output);
	if (checkannotations(callback.position))
		output.pagenumber = TRUE;

				/* event loop */

	cairoui_main(&cairoui, firstwindow);

	closepdf(callback.position);
	if (keepopen != -1)
		close(keepopen);
	return EXIT_SUCCESS;
}

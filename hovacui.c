/*
 * hovacui.c
 *
 * view a pdf document, autozooming to text
 */

/*
 * todo:
 *
 * - search: regular expression; config for pattern=string
 * - pass a configuration option from command line
 * - configuration files specific for the framebuffer and x11, passed as
 *   additional arguments to hovacui()
 *   .config/hovacui/{framebuffer.conf,x11.conf}
 * - utf8 or widechar in cairodevice->input() and field()
 * - merge narrow textarea boxes with others, minimizing size increase
 * - merge boxes with the same (or very similar) horizontal coordinates
 * - bookmarks, with field() for creating and list() for going to
 * - save last position(s) to $HOME/.pdfpositions
 * - allow cursor moves in field()
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
 *   progress label during save: requires savepdf to be a window, so that it
 *   can return WINDOW_REFRESH and be called again at each step
 *
 * - printf format string for page number label, with total pages
 * - make minwidth depend on the size of the letters
 * - list() yes/no to confirm exit; disabled by config option
 * - allow reloading during search
 * - man page: compare with fbpdf and jfbview
 * - field() for executing a shell command
 * - allow tabs in list(), make it display a table rather than a list;
 *   use in help() to separate keys from functions,
 *   in menu() to show the current values for viewmode, fit, etc.
 * - info(), based on list(): filename, number of pages, page size, etc.
 * - rotate
 * - lines of previous and next scroll: where the top or bottom of the screen
 *   were before the last scroll, or will be after scrolling up or down
 * - stack of windows; a window returns WINDOW_PREVIOUS to go back
 * - support 8bpp framebuffers (via a fixed colormap)
 * - cache the textarea list of pages already scanned
 * - config opt "nolabel" for no label at all: skip the label part from draw()
 * - multiple files, list()-based window; return WINDOW_FILE+n to tell main()
 *   which file to switch to; and/or have a field in struct output for the new
 *   file index or name
 * - optionally, show opening error as a window instead of fprintf
 * - allow for a password (field() or commandline)
 * - last two require document() to show a black screen if no document is open
 * - non-expert mode: every unassigned key calls WINDOW_MENU
 * - in list(): separator, skip it when using a selected line
 * - add an optional help to the bottom of list()
 * - clip window title in list()
 * - history of searches
 * - numbermode: 2 is a, 22 is b, 222 is c, etc.
 * - split pdf viewing functions to pdfview.c and gui stuff to cairogui.c
 * - include images (in pdfrects.c)
 * - key to reset viewmode and fit direction to initial values
 * - history of positions
 * - order of rectangles for right-to-left and top-to-bottom scripts
 *   (generalize sorting functions in pdfrects.c)
 * - i18n
 * - function to be possibly called before list() to wrap lines too long
 * - annotations and links:
 *   some key switches to anchor navigation mode, where keyup/keydown move to
 *   the next anchor (annotation or link) in displayed part of the current
 *   textbox (if any, otherwise they scroll as usual); this requires storing
 *   the current anchor both for its attribute (the text or the target) and for
 *   moving to the next; it was not needed for search, where the next match is
 *   just the first outside the area of the current textbox that is currently
 *   displayed
 * - detect file changes via inotify; this requires the file descriptor to be
 *   passed to cairodevice->input(); return KEY_FILECHANGE; the pdf file may
 *   not being fully written at that point, which results in a sequence of
 *   opening failures before being able to read it again; the current method
 *   using SIGHUP is better, if there is some way to send a signal when the
 *   file change is completed
 * - selection by cursor: requires cursor navigation, to be activated by some
 *   key; then mark start and end of selection
 * - select what shown: cheap alternative for selecting text: copy to file only
 *   the text that is in the screen and in the current textbox
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
 * the first match is located by scanning the document from the current textbox
 * to the last of the page, then in the following pages, counting the number of
 * pages until the number of pages in the document is reached
 *
 * matches that are in the current textbox but fall before the part of it that
 * is currently displayed on the screen are ignored
 *
 * the next match is the same but also excludes matches that are inside the
 * displayed part of the current textbox
 */

/*
 * user interface
 * --------------
 *
 * a simple user interface is implemented: windows receive input, labels do
 * not; both are functions with state stored as static variables or fields in
 * the output structure
 *
 * int window(int c, struct position *position, struct output *output);
 *	the given window is activated (if not already) and receive input c:
 *	- input is a key, but can also be KEY_INIT, KEY_REDRAW or KEY_REFRESH
 *	- output is the next window to become active
 *
 * void label(struct position *position, struct output *output);
 *	all label functions are called at each step; they have to decide by
 *	themselves whether to draw something; they choose based on the content
 *	of the position and output structures; for example, the pagenumber()
 *	labels draws when the page number changes and when output->pagenumber
 *	is true
 *
 * each window is a specific instance of a generic window:
 *
 * field()
 *	a generic textfield
 *	called by search()
 *
 * number()
 *	a textfield for a number; calls field()
 *	called by gotopage(), minwidth() and textdistance()
 *
 * list()
 *	a list of strings, possibly with a selected one
 *	called by help(), tutorial() and menu()
 *
 * the generic windows draw and set output->flush; a specific window
 * preprocesses the input character if necessary, then calls the generic window
 * and processes its return value, possibly changing the output and the
 * position structures; it returns the next window, or WINDOW_REFRESH to have
 * the document be redrawn and be called again with input KEY_REFRESH
 *
 * a particular window is document(), which draws nothing and deal with normal
 * input (when no other window is active); this is the only window that can set
 * output->redraw to redraw the document
 *
 * the document is not redrawn at each step; rather, windows and labels are
 * just drawn over it, and therefore remain on the screen until the document is
 * redrawn; the document is redrawn only when:
 * - the input timeout was set >0 and it either expires or a key arrived
 * - the virtual terminal is switched in
 * - document() sets output->redraw; it does when it changes position
 * - a window other than WINDOW_DOCUMENT returns another window
 * - a window or external command returns WINDOW_REFRESH
 *
 * a window requests probing the input by setting output->timeout=0 and
 * returning itself; the document is redrawn only if output->redraw is set;
 * instead, WINDOW_REFRESH does no input probe and always redraws the document
 */

/*
 * note: the main loop
 *
 * is a sequence of three steps, each skipped in certain situations
 *
 * 1. draw the document, call the label functions, flush
 *	nothing is done if the output is not active (vt switched out)
 *	drawing and flushing depends on output->redraw and output->flush
 *
 * 2. receive input
 *	actual input is read only if c == KEY_NONE
 *	input may be KEY_TIMEOUT, KEY_REDRAW, etc. (see below)
 *	in some cases this step ends with a "continue" to skip step 3
 *
 * 3. call the window function or the external command function
 *	draw the window
 *	set output->redraw or output->flush if necessary
 *
 * relevant variables:
 * c		if an input character is read, it is stored in this variable;
 *		special values KEY_* carry instructions for the next iteration
 *		such as skipping input and refreshing the screen instead, etc.
 * window	the current window
 * next		the next window, or WINDOW_REFRESH
 * output.redraw whether to redraw the document at the next step 1
 * output.flush	whether to flush the document afterward
 *
 * example: change a page by an external command while the main menu is active;
 * external commands are read in step 2; step 3 calls the external() function,
 * which changes the page number and returns WINDOW_REFRESH; this causes step 3
 * to set output->redraw=TRUE, output->flush=FALSE and c=KEY_REFRESH; in step
 * 1, output->redraw=TRUE causes the document to be redrawn, and output->redraw
 * to be set to false; when the pagelabel() function is called it detects a
 * page change and draws the page number label; flushing is not done because
 * output->flush=FALSE; step 2 skips reading input since c is not KEY_NONE;
 * step 3 calls the menu window with c=KEY_REFRESH, which causes it to redraw
 * itself and set output->flush=TRUE; step 1 this time has output->redraw=FALSE
 * and output->flush=TRUE, and therefore flushes the output
 */

/*
 * note: input
 *
 * a window is a function; one of its arguments is the user input, represented
 * as an integer; when the window is open, it is called on each keystroke, and
 * may also be called with one of the imaginary keys:
 *
 * KEY_NONE
 *	no window ever receives this
 *
 * KEY_INIT
 *	upon opening, the window is called the first time with this value;
 *	every input different than KEY_INIT indicate that the window has not
 *	been closed in the meantime
 *
 * KEY_REDRAW
 *	received when the window has to redraw itself because of an external
 *	reason (the terminal has been switched in)
 *
 * KEY_REFRESH
 *	the window has requested a redraw of the document or the labels by
 *	returning WINDOW_REFRESH, and now is its turn to redraw itself
 *
 * KEY_RESIZE
 *	the space where the pdf is drawn has been resized
 *
 * KEY_TIMEOUT
 *	user input is normally waited with no time limit, but windows and
 *	labels may set a timeout in output->timeout; this value tells that the
 *	timeout expired; windows generally ignore this value
 *
 * KEY_SUSPEND
 *	the program should not draw anything
 *
 * KEY_SIGNAL
 *	the input select() was interrupted by a signal; none of the windows use
 *	this value
 *
 * KEY_EXTERNAL
 *	an external command come from the input fifo; no window receives this
 */

/*
 * note: fit=none
 * --------------
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
 * a file is reloaded in the main loop whenever output->reload=TRUE
 *
 * this field is set in:
 *
 * - document(), upon receiving keystroke 'r'
 * - external(), upon receiving the external command "reload"
 * - draw(), if a file change is detected (see below)
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
 * note: page by page search
 *
 * search() only searches in one page, then probes the input before searching
 * in the next; probing requires returning to the main loop, which calls it
 * back with the result; this means that search() is called in two cases:
 *
 * - to receive the keystrokes that make the sequence to search for
 * - during search, to receive the result of probing
 *
 * when initialized, search() sets the nsearched static variable to -1 to
 * memorize that search has not begun yet; when search begins, it sets
 * nsearched to 0, and increases it at each page searched; search stops when
 * the pattern is found or this number reaches the number of pages in the
 * document
 *
 * next() is similar, but nsearched cannot be -1 because this function does not
 * receive input keystrokes: it is called on 'n' or 'p' to find the next or
 * previous match
 */

/*
 * note: the cairodevice
 *
 * the struct cairodevice allows hovacui to be run whenever a cairo context can
 * be obtained and input is available
 *
 * hovacui() receives one such structure; it uses its functions for creating
 * the cairo context, for drawing and getting input to the aim of showing the
 * pdf file until the keystroke 'q' is received
 *
 * void *initdata
 *	passed to the init function
 *
 * struct cairooutput *init(char *device, void *initdata);
 *	create the cairo context;
 *	return a data structure that is passed to the other functions
 *
 * void finish(struct cairooutput *cairo);
 *	undo what done by init
 *
 * cairo_t *context(struct cairooutput *cairo);
 * double width(struct cairooutput *cairo);
 * double height(struct cairooutput *cairo);
 * double screenwidth(struct cairooutput *cairo);
 * double screenheight(struct cairooutput *cairo);
 *	return the cairo context and its size
 *
 * void clear(struct cairooutput *cairo);
 * void flush(struct cairooutput *cairo);
 *	clear and flush
 *
 * int isactive(struct cairooutput *cairo);
 *	whether the output is active
 *	do not draw on the framebuffer when the vt is switched out
 *
 * int input(struct cairooutput *cairo, int timeout, struct command *command);
 *	return a key
 *	on external command: store it in command->string, return KEY_EXTERNAL
 *	if no input is available block for timeout millisecond (0=infinite)
 */

/*
 * note: paste
 *
 * paste is implemented by cairodevice->input storing the text in
 * command->string and returning KEY_PASTE; the active window decides what to
 * do with this text: field() includes it, the other windows ignore it; more
 * generally, only the windows waiting for a string use the pasted text; the
 * windows expecting a command in form of a keystroke do not
 *
 * paste is implemented in both xhovacui and fbhovacui; the latter is not
 * obvious because, by default, a virtual terminal sends the pasted text one
 * character at time from stdin as if they were regular keystrokes;
 * fbhovacui->input tell the difference by an heuristics: if four or more
 * characters are available after one select, they are pasted text; this works
 * reasonably in most practical cases, since pasted text is typically not too
 * short, and three characters or less that are interpreted as commands instead
 * of text should not be much of a problem anyway
 *
 * the alternative is to receive mouse events instead; but ncurses does not
 * expose the mouse file descriptor to be used in a select; event if it did,
 * the linux kernel does not allow to retrieve the selection; it only allows it
 * to be fed to the virtual terminal as regular input characters
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
#include "hovacui.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

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
	WINDOW_MENU,
	WINDOW_WIDTH,
	WINDOW_DISTANCE,
	WINDOW_REFRESH,
	WINDOW_EXIT
};

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

	/* the pixel aspect */
	double aspect;

	/* the minimal textbox-to-textbox distance */
	double distance;

	/* the minimal width; tells the maximal zoom */
	double minwidth;

	/* zoom to: 0=text, 1=boundingbox, 2=page */
	int viewmode;

	/* fit horizontally (1), vertically (2) or both (0) */
	int fit;

	/* sorting algorithm */
	int order;

	/* scroll distance, as a fraction of the screen size */
	double scroll;

	/* show the ui (menu and help) */
	int ui;

	/* apply the changes immediately from the ui */
	int immediate;

	/* draw draw the textbox and page box */
	int drawbox;

	/* show the page number when it changes */
	int pagelabel;

	/* whether the document has to be reloaded */
	int reload;

	/* whether the document has to be redrawn */
	int redraw;

	/* whether the output is to be flushed */
	int flush;

	/* if not NO_TIMEOUT, stop input on timeout and return KEY_TIMEOUT */
	int timeout;

	/* labels */
	gboolean pagenumber;
	gboolean totalpages;
	gboolean showmode;
	gboolean showfit;
	gboolean filename;
	char help[80];

	/* size of font */
	cairo_font_extents_t extents;

	/* search */
	char search[100];
	gboolean forward;
	GList *found;

	/* selection */
	GList *selection;
	double texfudge;

	/* text output and logging file */
	int log;
	char *outname;
	FILE *outfile;

	/* pdf output */
	char *pdfout;
	int first, last;
	char *postsave;

	/* pasted text */
	char *paste;
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
 * ensure the output file is open
 */
int ensureoutputfile(struct output *output) {
	if (output->outfile != NULL)
		return 0;
	output->outfile = fopen(output->outname, "w");
	return output->outfile == NULL;
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
		output->reload = TRUE;
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
		position->boundingbox =
			rectanglelist_boundingbox(position->page);
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
	    (fitmode == 1 && viewbox->x2 - viewbox->x1 < minwidth)) {
		d = minwidth - viewbox->x2 + viewbox->x1;
		viewbox->x1 -= d / 2;
		viewbox->x2 += d / 2;
	}

	if (fitmode == 0 ||
	    (fitmode == 2 && viewbox->y2 - viewbox->y1 < minheight)) {
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
 * go to the first or next match in the page, if any
 */
int nextpagematch(struct position *position, struct output *output,
		gboolean inscreen, gboolean first) {
	gboolean forward;
	int b;
	int end, step;
	double width, height, prev;
	PopplerRectangle *t, r;
	GList *o, *l;

	if (output->found == NULL)
		return -1;

	forward = output->forward;
	end = forward ? position->textarea->num : -1;
	step = forward ? +1 : -1;

	if (forward)
		o = output->found;
	else {
		o = g_list_copy(output->found);
		o = g_list_reverse(o);
	}

	poppler_page_get_size(position->page, &width, &height);
	for (b = position->box; b != end; b += step) {
		t = &position->textarea->rect[b];
		for (l = o; l != NULL; l = l->next) {
			r = * (PopplerRectangle *) l->data;
			prev = r.y1;
			r.y1 = height - r.y2;
			r.y2 = height - prev;

			if (! rectangle_contain(t, &r))
				continue;
			if (first &&
			    ! relativescreen(output, &r, inscreen, forward))
				continue;

			position->box = b;
			scrolltorectangle(position, output, &r,
			                  forward, ! forward);

			if (! forward)
				g_list_free(o);
			return 0;
		}
		inscreen = TRUE;
		first = FALSE;
	}

	if (! forward)
		g_list_free(o);
	return -1;
}

/*
 * go to the first/next match in the document
 */
int gotomatch(struct position *position, struct output *output,
		int nsearched, int inscreen) {
	static struct position scan;

	if (nsearched == 0) {
		if (output->search[0] == '\0')
			return -2;
		scan = *position;
		g_object_ref(scan.page);
		scan.textarea = rectanglelist_copy(position->textarea);
		scan.boundingbox = poppler_rectangle_copy(scan.boundingbox);
		scan.viewbox = NULL; // do not free, is also in *position
		moveto(&scan, output);
	}
	else if (nsearched == -1) {
		rectanglelist_free(scan.textarea);
		poppler_rectangle_free(scan.boundingbox);
		poppler_rectangle_free(scan.viewbox);
		return -1;
	}

	pagematch(&scan, output);
	if (output->found != NULL && nsearched > 0) {
		textarea(&scan, output);
		scan.box = output->forward ? 0 : (scan.textarea->num - 1);
	}

	if (! ! nextpagematch(&scan, output, inscreen, nsearched == 0)) {
		scan.npage = (scan.npage + (output->forward ? 1 : -1)
			+ scan.totpages) % scan.totpages;
		readpageraw(&scan, output);
		nsearched++;
		return scan.npage;
	}

	rectanglelist_free(position->textarea);
	poppler_rectangle_free(position->boundingbox);
	poppler_rectangle_free(position->viewbox);
	scan.permanent_id = position->permanent_id;
	scan.update_id = position->update_id;
	g_set_object(&position->page, scan.page);
	g_object_unref(scan.page);
	*position = scan;
	return -1;
}

/*
 * formatted print to the help label; timeout=NO_TIMEOUT means infinite
 */
int printhelp(struct output *output, int timeout, char *format, ...) {
	va_list ap;
	int res;

	va_start(ap, format);
	res = vsnprintf(output->help, 80, format, ap);
	va_end(ap);

	output->timeout = timeout;
	output->flush = TRUE;

	return res;
}

/*
 * move to a given page
 */
int movetopage(struct position *position, struct output *output, int page) {
	if (page < 1 || page > position->totpages) {
		printhelp(output, 2000, "no such page: %d", page);
		return -1;
	}
	if (page - 1 == position->npage)
		return -2;
	position->npage = page - 1;
	readpage(position, output);
	return firsttextbox(position, output);
}

/*
 * move to a named destination
 */
int movetonameddestination(struct position *position, struct output *output,
		char *name) {
	PopplerDest *dest;
	double width, height;
	PopplerRectangle r, *s, *p;
	int pos;

	dest = poppler_document_find_dest(position->doc, name);
	if (dest == NULL) {
		printhelp(output, 2000, "no such destination: %s", name);
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
 * save a selection of pages to file
 */
cairo_status_t writetofile(void *c, const unsigned char *d, unsigned int l) {
	return 1 == fwrite(d, l, 1, (FILE *) c) ?
		CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}
int savepdf(PopplerDocument *doc, char *pattern,
		int first, int last, PopplerRectangle *r) {
	FILE *out;
	PopplerPage *page;
	cairo_surface_t *surface;
	cairo_t *cr;
	int file;
	int tot, n;
	gdouble width, height;

	tot = poppler_document_get_n_pages(doc);
	if (first < 0 || last > tot - 1 || last < first)
		return -1;

	out = firstfree(pattern, &file);
	if (out == NULL)
		return -1;

	surface = cairo_pdf_surface_create_for_stream(writetofile, out, 1, 1);
	cr = cairo_create(surface);
	for (n = first; n <= last; n++) {
		page = poppler_document_get_page(doc, n);
		if (page == NULL)
			break;
		poppler_page_get_size(page, &width, &height);
		if (r == NULL)
			cairo_pdf_surface_set_size(surface, width, height);
		else {
			cairo_pdf_surface_set_size(surface,
				r->x2 - r->x1 + 10, r->y2 - r->y1 + 10);
			cairo_translate(cr, -r->x1 + 5, -r->y1 + 5);
			cairo_rectangle(cr, r->x1, r->y1,
				r->x2 - r->x1, r->y2 - r->y1);
			cairo_clip(cr);
		}
		poppler_page_render_for_printing(page, cr);
		cairo_surface_show_page(surface);
		g_object_unref(page);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	fclose(out);
	return file;
}

/*
 * save current textbox to file
 */
int savecurrenttextbox(struct position *position, struct output *output) {
	int o;
	char *fmt, *command;

	o = savepdf(position->doc, output->pdfout,
			position->npage, position->npage,
			&position->textarea->rect[position->box]);
	if (o < 0) {
		printhelp(output, 3000, "error saving pdf");
		return o;
	}

	fmt = malloc(strlen(output->pdfout) + 100);
	sprintf(fmt, "saved current textbox to %s", output->pdfout);
	printhelp(output, 3000, fmt, o);
	free(fmt);

	if (output->postsave != NULL) {
		command = malloc(strlen(output->postsave) + 100);
		sprintf(command, output->postsave, o, o);
		system(command);
		free(command);
	}

	return 0;
}

/*
 * print the current box and append it to file
 */
int savebox(struct position *position, struct output *output) {
	PopplerRectangle r;
	char line[70];
	char *result;

	r = position->textarea->rect[position->box];
	snprintf(line, 70, "%g %g %g %g", r.x1, r.y1, r.x2, r.y2);

	if (ensureoutputfile(output))
		result = "- error opening output file";
	else {
		rectangle_print(output->outfile, &r);
		fputs("\n", output->outfile);
		fflush(output->outfile);
		result = "- saved to";
	}

	printhelp(output, 2000, "%s %s %s", line, result, output->outname);
	return 0;
}

/*
 * document window
 */
int document(int c, struct position *position, struct output *output) {
	switch (c) {
	case 'r':
		output->reload = TRUE;
		output->redraw = TRUE;
		break;
	case KEY_INIT:
	case KEY_TIMEOUT:
	case KEY_REDRAW:
	case KEY_RESIZE:
		return WINDOW_DOCUMENT;
	case KEY_REFRESH:
		output->flush = TRUE;
		return WINDOW_DOCUMENT;
	case 'q':
		return WINDOW_EXIT;
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
		savecurrenttextbox(position, output);
		break;
	case 'w':
		return WINDOW_WIDTH;
	case 't':
		return WINDOW_DISTANCE;
	case 'o':
		return WINDOW_ORDER;
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
		if (output->minwidth <= 0)
			break;
		output->minwidth -= 10;
		if (output->fit & 0x1)
			position->scrollx = 0;
		else if (output->fit & 0x2)
			position->scrolly = 0;
		break;
	case 'Z':
		if (boundingboxinscreen(position, output))
			break;
		output->minwidth += 10;
		break;
	case 'f':
		output->fit = (output->fit + 1) % 4;
		position->scrollx = 0;
		position->scrolly = 0;
		break;
	case 's':
		output->timeout = 3000;
		output->pagenumber = TRUE;
		output->showmode = TRUE;
		output->showfit = TRUE;
		output->filename = TRUE;
		break;
	case 'b':
		savebox(position, output);
		break;
	case '\\':	/* for testing */
		movetonameddestination(position, output, "abcd");
		break;
	default:
		;
	}

	output->redraw = TRUE;
	output->flush = TRUE;
	return WINDOW_DOCUMENT;
}

/*
 * print a line at current position
 */
void printline(cairo_t *cr, char *string, int advance) {
	double x, y;
	cairo_get_current_point(cr, &x, &y);
	cairo_show_text(cr, string);
	cairo_move_to(cr, x, y + advance);
}

/*
 * a list of strings, possibly with a selected one
 */
int list(int c, struct output *output, char *viewtext[],
		int *line, int *selected) {
	double percent = 0.8;
	double width = output->dest.x2 - output->dest.x1;
	double height = output->dest.y2 - output->dest.y1;
	double marginx = width * (1 - percent) / 2;
	double marginy = height * (1 - percent) / 2;
	double borderx = 10.0;
	double bordery = 10.0;
	double titleheight = output->extents.height + 2 * bordery;
	double textheight, listheight;
	double startx = output->dest.x1 + marginx;
	double starty = output->dest.y1 + marginy;
	double startlist = starty + titleheight + bordery;
	int n, l, lines;

	for (n = 1; viewtext[n] != NULL; n++) {
	}

	cairo_identity_matrix(output->cr);
	lines = (int) (height * percent - titleheight - bordery * 2) /
		(int) output->extents.height;
	textheight = (n - 1 < lines ? n - 1 : lines) * output->extents.height;
	listheight = textheight + 2 * bordery;
	height = textheight + listheight;

	switch (c) {
	case KEY_DOWN:
		if (selected != NULL && *selected >= n - 1)
			return 0;
		if (selected != NULL && *selected < *line + lines)
			(*selected)++;
		else if (*line >= n - lines - 1)
			return 0;
		else {
			if (selected != NULL)
				(*selected)++;
			(*line)++;
		}
		break;
	case KEY_UP:
		if (selected != NULL && *selected <= 1)
			return 0;
		else if(selected != NULL && *selected > *line + 1)
			(*selected)--;
		else if (*line <= 0)
			return 0;
		else {
			if (selected != NULL)
				(*selected)--;
			(*line)--;
		}
		break;
	case KEY_INIT:
	case KEY_REDRAW:
	case KEY_RESIZE:
	case KEY_REFRESH:
		break;
	case '\033':
	case KEY_EXIT:
		return -1;
	case KEY_ENTER:
	case '\n':
		return selected != NULL ? *selected : -1;
	default:
		return selected != NULL ? 0 : -1;
	}

				/* list heading */

	cairo_set_source_rgb(output->cr, 0.6, 0.6, 0.8);
	cairo_rectangle(output->cr,
		startx, starty, width - marginx * 2, titleheight);
	cairo_fill(output->cr);
	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		startx + borderx, starty + bordery + output->extents.ascent);
	printline(output->cr, viewtext[0], output->extents.height);

				/* clip to make the list scrollable */

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx,
		output->dest.y1 + marginy + titleheight,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		listheight);
	cairo_fill(output->cr);

				/* background */

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_save(output->cr);
	cairo_rectangle(output->cr,
		output->dest.x1 + marginx, startlist,
		width - marginx * 2, textheight);
	cairo_clip(output->cr);

				/* draw the list elements */

	cairo_translate(output->cr, 0.0, - output->extents.height * *line);
	for (l = 1; viewtext[l] != NULL; l++) {
		if (selected == NULL || l != *selected)
			cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
		else {
			cairo_set_source_rgb(output->cr, 0.3, 0.3, 0.3);
			cairo_rectangle(output->cr,
				startx,
				startlist + output->extents.height * (l - 1),
				width - 2 * marginx,
				output->extents.height);
			cairo_fill(output->cr);
			cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
		}
		cairo_move_to(output->cr,
			startx + borderx,
			startlist + output->extents.height * (l - 1) +
				output->extents.ascent);
		cairo_show_text(output->cr, viewtext[l]);
	}
	cairo_stroke(output->cr);
	cairo_restore(output->cr);

				/* draw the scrollbar */

	if (lines < n - 1) {
		cairo_rectangle(output->cr,
			output->dest.x2 - marginx - borderx,
			output->dest.y1 + marginy + titleheight +
				*line / (double) (n - 1) * listheight,
			borderx,
			lines / (double) (n - 1) * listheight);
		cairo_fill(output->cr);
		cairo_stroke(output->cr);
	}

	output->flush = TRUE;
	return 0;
}

/*
 * help
 */
int help(int c, struct position *position, struct output *output) {
	static char *helptext[] = {
		"hovacui - pdf viewer with autozoom to text",
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
	(void) position;

	return list(c, output, helptext, &line, NULL) == 0 ?
		WINDOW_HELP : WINDOW_DOCUMENT;
}

/*
 * tutorial
 */
int tutorial(int c, struct position *position, struct output *output) {
	static char *tutorialtext[] = {
		"hovacui - pdf viewer with autozoom to text",
		"hovacui displays a block of text at time",
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
	(void) position;

	for (i = 0; tutorialtext[i] != NULL; i++)
		if (strstr(tutorialtext[i], "%s")) {
			sprintf(cursor, tutorialtext[i],
				output->fit == 1 ? " Up/Down" : "",
				output->fit == 2 ? " Left/Right" : "");
			tutorialtext[i] = cursor;
		}

	return c == 'h' ? WINDOW_HELP :
		list(c, output, tutorialtext, &line, NULL) == 0 ?
			WINDOW_TUTORIAL : WINDOW_DOCUMENT;
}

/*
 * chop menu, to save a range of pages
 */
int chop(int c, struct position *position, struct output *output) {
	static char *choptext[] = {
		"page range",
		"first page",
		"last page",
		"save range",
		"clear range",
		"save document",
		"save current page",
		"save current box",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res, o;
	int first, last;
	char *fmt;

	if (output->first != -1 && output->last != -1)
		printhelp(output, NO_TIMEOUT, "range: %d-%d",
			output->first + 1, output->last + 1);
	else if (output->first != -1)
		printhelp(output, NO_TIMEOUT, "range: %d-", output->first + 1);
	else if (output->last != -1)
		printhelp(output, NO_TIMEOUT, "range: -%d", output->last + 1);
	else
		output->help[0] = '\0';

	if (c == KEY_INIT)
		selected = output->first == -1 ? 1 : output->last == -1 ? 2 : 3;

	res = list(c, output, choptext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_CHOP;
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
		o = savepdf(position->doc, output->pdfout, first, last, NULL);
		if (o < 0) {
			printhelp(output, 3000, "error saving pdf");
			break;
		}
		fmt = malloc(strlen(output->pdfout) + 100);
		sprintf(fmt, "saved pages %%d-%%d to %s", output->pdfout);
		printhelp(output, 3000, fmt, first + 1, last + 1, o);
		free(fmt);
		output->first = -1;
		output->last = -1;
		break;
	case 4:
		output->first = -1;
		output->last = -1;
		output->help[0] = '\0';
		break;
	case 5:
		first = 0;
		last = position->totpages - 1;
		o = savepdf(position->doc, output->pdfout, first, last, NULL);
		if (o < 0) {
			printhelp(output, 3000, "error saving pdf");
			break;
		}
		fmt = malloc(strlen(output->pdfout) + 100);
		sprintf(fmt, "saved document to %s", output->pdfout);
		printhelp(output, 3000, fmt, o);
		free(fmt);
		break;
	case 6:
		o = savepdf(position->doc, output->pdfout,
			position->npage, position->npage, NULL);
		if (o < 0) {
			printhelp(output, 3000, "error saving pdf");
			break;
		}
		fmt = malloc(strlen(output->pdfout) + 100);
		sprintf(fmt, "saved page %%d to %s", output->pdfout);
		printhelp(output, 3000, fmt, position->npage + 1, o);
		free(fmt);
		break;
	case 7:
		savecurrenttextbox(position, output);
		break;
	}

	return WINDOW_DOCUMENT;
}

/*
 * viewmode menu
 */
int viewmode(int c, struct position *position, struct output *output) {
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

	res = list(c, output, viewmodetext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_VIEWMODE;
	case 1:
	case 2:
	case 3:
	case 4:
		output->viewmode = res - 1;
		textarea(position, output);
		firsttextbox(position, output);
		if (output->immediate)
			return WINDOW_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * fit direction menu
 */
int fitdirection(int c, struct position *position, struct output *output) {
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

	res = list(c, output, fitdirectiontext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_FITDIRECTION;
	case 1:
	case 2:
	case 3:
	case 4:
		output->fit = res - 1;
		firsttextbox(position, output);
		if (output->immediate)
			return WINDOW_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * sorting algorithm menu
 */
int order(int c, struct position *position, struct output *output) {
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

	res = list(c, output, fitdirectiontext, &line, &selected);
	switch (res) {
	case 0:
		return WINDOW_ORDER;
	case 1:
	case 2:
	case 3:
	case 4:
		output->order = res - 1;
		textarea(position, output);
		firsttextbox(position, output);
		if (output->immediate)
			return WINDOW_REFRESH;
		/* fallthrough */
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * main menu
 */
int menu(int c, struct position *position, struct output *output) {
	static char *menutext[] = {
		"hovacui - menu",
		"(g) go to page",
		"(/) search",
		"(c) save document or page selection",
		"(v) view mode",
		"(f) fit direction",
		"(w) minimal width",
		"(t) text distance",
		"(o) block order",
		"(h) help",
		"(q) quit",
		NULL
	};
	static char *shortcuts = "g/cvfwtohq", *s;
	static int menunext[] = {
		WINDOW_MENU,
		WINDOW_GOTOPAGE,
		WINDOW_SEARCH,
		WINDOW_CHOP,
		WINDOW_VIEWMODE,
		WINDOW_FITDIRECTION,
		WINDOW_WIDTH,
		WINDOW_DISTANCE,
		WINDOW_ORDER,
		WINDOW_HELP,
		WINDOW_EXIT,
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

	res = s == NULL ?
		list(c, output, menutext, &line, &selected) :
		s - shortcuts + 1;

	if (res >= 0 && res < n)
		return menunext[res];
	if (res >= n)
		printhelp(output, 2000, "unimplemented");
	return WINDOW_DOCUMENT;
}

/*
 * generic textfield
 */
#define FIELD_DONE 0
#define FIELD_LEAVE 1
#define FIELD_INVALID 2
#define FIELD_UNCHANGED 3
#define FIELD_CHANGED 4
int field(int c, struct output *output,
		char *prompt, char *current, char *error, char *help) {
	double percent = 0.8, prop = (1 - percent) / 2;
	double marginx = (output->dest.x2 - output->dest.x1) * prop;
	double marginy = 20.0;
	double startx = output->dest.x1 + marginx;
	double starty = output->dest.y1 + marginy;
	cairo_text_extents_t te;
	int l, i;

	if (c == '\033' || c == KEY_EXIT)
		return FIELD_LEAVE;
	if (c == '\n' || c == KEY_ENTER)
		return FIELD_DONE;

	l = strlen(current);
	if (c == KEY_BACKSPACE || c == KEY_DC) {
		if (l < 0)
			return FIELD_UNCHANGED;
		current[l - 1] = '\0';
	}
	else if (c == KEY_PASTE) {
		if (l > 30)
			return FIELD_UNCHANGED;
		for (i = 0; output->paste[i] != '\0' && l <= 30; i++, l++)
			current[l] = output->paste[i];
		current[l] = '\0';
	}
	else if (c < KEY_NONE &&
	         c != KEY_REDRAW && c != KEY_REFRESH && c != KEY_RESIZE) {
		if (l > 30)
			return FIELD_UNCHANGED;
		current[l] = c;
		current[l + 1] = '\0';
	}
	else if (help != NULL)
		printhelp(output, NO_TIMEOUT, help);

	output->flush = TRUE;

	cairo_identity_matrix(output->cr);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_rectangle(output->cr,
		startx,
		starty,
		output->dest.x2 - output->dest.x1 - marginx * 2,
		output->extents.height + 10);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.0, 0.0, 0.0);
	cairo_move_to(output->cr,
		startx + 10.0,
		starty + 5.0 + output->extents.ascent);
	cairo_show_text(output->cr, prompt);
	cairo_show_text(output->cr, current);
	cairo_show_text(output->cr, "_ ");
	if (error == NULL)
		return FIELD_CHANGED;
	cairo_text_extents(output->cr, error, &te);
	cairo_set_source_rgb(output->cr, 0.8, 0.0, 0.0);
	cairo_rectangle(output->cr,
		output->dest.x2 - marginx - te.x_advance - 20.0,
		starty,
		te.x_advance + 20.0,
		output->extents.height + 10.0);
	cairo_fill(output->cr);
	cairo_move_to(output->cr,
		output->dest.x2 - marginx - te.x_advance - 10.0,
		starty + 5.0 + output->extents.ascent);
	cairo_set_source_rgb(output->cr, 1.0, 1.0, 1.0);
	cairo_show_text(output->cr, error);
	return FIELD_CHANGED;
}

/*
 * keys always allowed for a field
 */
int keyfield(int c) {
	return c == KEY_INIT ||
		c == KEY_REDRAW || c == KEY_REFRESH || c == KEY_RESIZE ||
		c == KEY_BACKSPACE || c == KEY_DC ||
		c == KEY_ENTER || c == '\n' ||
		c == '\033' || c == KEY_EXIT;
}

/*
 * allowed input for a numeric field
 */
int keynumeric(int c) {
	return (c >= '0' && c <= '9') || keyfield(c);
}

/*
 * generic field for a number
 */
int number(int c, struct output *output,
		char *prompt, char *current, char *error, char *help,
		double *destination, double min, double max) {
	double n;
	int res;

	switch (c) {
	case 'q':
		c = KEY_EXIT;
		break;

	case KEY_INIT:
		sprintf(current, "%lg", *destination);
		break;

	case KEY_DOWN:
	case KEY_UP:
		n = current[0] == '\0' ? *destination : atof(current);
		n = n + (c == KEY_DOWN ? +1 : -1);
		if (n < min) {
			if (c == KEY_DOWN)
				n = min;
			else
				return FIELD_UNCHANGED;
		}
		if (n > max) {
			if (c == KEY_UP)
				n = max;
			else
				return FIELD_UNCHANGED;
		}
		sprintf(current, "%lg", n);
		c = KEY_REDRAW;
		break;

	default:
		if (! keynumeric(c))
			return FIELD_UNCHANGED;
	}

	res = field(c, output, prompt, current, error, help);
	if (res == FIELD_DONE) {
		if (current[0] == '\0')
			return FIELD_LEAVE;
		n = atof(current);
		if (n < min || n > max)
			return FIELD_INVALID;
		*destination = n;
	}
	return res;
}

/*
 * field for a search keyword
 */
int search(int c, struct position *position, struct output *output) {
	static char searchstring[100] = "", prevstring[100] = "";
	static int nsearched = 0;
	char *prompt = "find: ";
	int res, page;

	if (c == KEY_INIT)
		nsearched = 0;

	if (nsearched == -1 || nsearched > position->totpages) {
		if (c == KEY_TIMEOUT || c == KEY_REFRESH) {
			field(KEY_REDRAW, output, prompt, searchstring,
				nsearched == -1 ? "stopped" : "no match",
				NULL);
			return WINDOW_SEARCH;
		}
		gotomatch(position, output, -1, FALSE);
		nsearched = 0;
	}

	if (nsearched == 0 && c == KEY_UP) {
		strcpy(searchstring, prevstring);
		c = KEY_NONE;
	}

	res = nsearched == 0 ?
		field(c, output, prompt, searchstring, NULL, NULL) :
		FIELD_DONE;

	if (res == FIELD_LEAVE) {
		strcpy(output->search, "");
		pagematch(position, output);
		strcpy(prevstring, searchstring);
		searchstring[0] = '\0';
		return WINDOW_DOCUMENT;
	}

	if (res == FIELD_DONE) {
		if (nsearched == 0) {
			strcpy(output->search, searchstring);
			if (searchstring[0] == '\0') {
				pagematch(position, output);
				return WINDOW_DOCUMENT;
			}
			field(KEY_REDRAW, output, prompt, searchstring,
				"searching", NULL);
		}

		page = gotomatch(position, output, nsearched, nsearched == 0);
		if (page == -1) {
			printhelp(output, 2000,
				"n=next matches p=previous matches");
			strcpy(prevstring, searchstring);
			searchstring[0] = '\0';
			return WINDOW_DOCUMENT;
		}

		output->timeout = 0;

		if (c == KEY_EXIT || c == '\033' || c == 's' || c == 'q') {
			readpage(position, output);
			output->redraw = 1;
			nsearched = -1;
			return WINDOW_SEARCH;
		}

		nsearched++;
		if (nsearched > position->totpages) {
			output->redraw = 1;
			printhelp(output, 0, "");
		}
		else
			printhelp(output, 0,
				"    searching page %-5d ", page + 1);
		return WINDOW_SEARCH;
	}

	return WINDOW_SEARCH;
}

/*
 * next match of a search
 */
int next(int c, struct position *position, struct output *output) {
	static int nsearched = 0;
	int page;

	if (c == KEY_INIT)
		nsearched = 0;

	if (nsearched == -1 || nsearched > position->totpages) {
		gotomatch(position, output, -1, FALSE);
		nsearched = 0;
		printhelp(output, 2000, "not found: %s\n", output->search);
		return WINDOW_DOCUMENT;
	}

	page = gotomatch(position, output, nsearched, FALSE);
	if (page == -1) {
		printhelp(output, 2000, "n=next matches p=previous matches");
		return WINDOW_DOCUMENT;
	}
	else if (page == -2) {
		printhelp(output, 2000, "no previous search");
		return WINDOW_DOCUMENT;
	}

	output->timeout = 0;

	if (c == KEY_EXIT || c == '\033' || c == 's' || c == 'q') {
		readpage(position, output);
		output->redraw = 1;
		nsearched = -1;
		return WINDOW_DOCUMENT;
	}

	nsearched++;
	if (nsearched > position->totpages) {
		output->redraw = 1;
		printhelp(output, 0, "");
	}
	else
		printhelp(output, 0, "    searching \"%s\" on page %-5d ",
			output->search, page + 1);
	return WINDOW_NEXT;
}

/*
 * field for a page number
 */
int gotopage(int c, struct position *position, struct output *output) {
	static char gotopagestring[100] = "";
	int res;
	double n;

	switch (c) {
	case KEY_INIT:
		c = KEY_REDRAW;
		break;
	case KEY_PPAGE:
	case KEY_NPAGE:
		c = c == KEY_PPAGE ? KEY_UP : KEY_DOWN;
		break;
	case 'c':
		sprintf(gotopagestring, "%d", position->npage + 1);
		c = KEY_REDRAW;
		break;
	case 'l':
		sprintf(gotopagestring, "%d", position->totpages);
		c = KEY_REDRAW;
		break;
	}

	n = position->npage + 1;
	res = number(c, output, "go to page: ", gotopagestring, NULL,
		"c=current l=last up=previous down=next enter=go",
		&n, 1, position->totpages);
	switch (res) {
	case FIELD_DONE:
		if (position->npage != n - 1) {
			position->npage = n - 1;
			readpage(position, output);
			firsttextbox(position, output);
		}
		if (output->immediate)
			return WINDOW_REFRESH;
		/* fallthrough */
	case FIELD_LEAVE:
		gotopagestring[0] = '\0';
		return WINDOW_DOCUMENT;
	case FIELD_INVALID:
		number(KEY_REDRAW, output,
			"go to page: ", gotopagestring, "no such page",
			"c=current e=end up=previous down=next enter=go",
			&n, 1, position->totpages);
		return WINDOW_GOTOPAGE;
	default:
		return WINDOW_GOTOPAGE;
	}
}

/*
 * field for the minimal width
 */
int minwidth(int c, struct position *position, struct output *output) {
	static char minwidthstring[100] = "";
	int res;

	res = number(c, output, "minimal width: ", minwidthstring, NULL,
		"down=increase enter=decrease", &output->minwidth, 0, 1000);
	if (res == FIELD_DONE) {
		readpage(position, output);
		firsttextbox(position, output);
		return output->immediate ? WINDOW_REFRESH : WINDOW_DOCUMENT;
	}
	return res == FIELD_LEAVE ? WINDOW_DOCUMENT : WINDOW_WIDTH;
}

/*
 * field for the text distance
 */
int textdistance(int c, struct position *position, struct output *output) {
	static char distancestring[100] = "";
	int res;

	res = number(c, output, "text distance: ", distancestring, NULL,
		"down=increase enter=decrease", &output->distance, 0, 1000);
	if (res == FIELD_DONE) {
		readpage(position, output);
		firsttextbox(position, output);
		return output->immediate ? WINDOW_REFRESH : WINDOW_DOCUMENT;
	}
	return res == FIELD_LEAVE ? WINDOW_DOCUMENT : WINDOW_DISTANCE;
}

/*
 * window selector
 */
int selectwindow(int window, int c,
		struct position *position, struct output *output) {
	switch (window) {
	case WINDOW_DOCUMENT:
		return document(c, position, output);
	case WINDOW_HELP:
		return help(c, position, output);
	case WINDOW_TUTORIAL:
		return tutorial(c, position, output);
	case WINDOW_MENU:
		return menu(c, position, output);
	case WINDOW_GOTOPAGE:
		return gotopage(c, position, output);
	case WINDOW_SEARCH:
		return search(c, position, output);
	case WINDOW_NEXT:
		return next(c, position, output);
	case WINDOW_CHOP:
		return chop(c, position, output);
	case WINDOW_VIEWMODE:
		return viewmode(c, position, output);
	case WINDOW_FITDIRECTION:
		return fitdirection(c, position, output);
	case WINDOW_WIDTH:
		return minwidth(c, position, output);
	case WINDOW_DISTANCE:
		return textdistance(c, position, output);
	case WINDOW_ORDER:
		return order(c, position, output);
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * show an arbitrary label at the given number of labels from the bottom
 */
void label(struct output *output, char *string, int bottom) {
	double width, x, y;

	cairo_identity_matrix(output->cr);

	width = strlen(string) * output->extents.max_x_advance;
	x = (output->dest.x2 + output->dest.x1) / 2 - width / 2;
	y = output->dest.y2 - bottom * (output->extents.height + 20.0 + 2.0);

	cairo_set_source_rgb(output->cr, 0, 0, 0);
	cairo_rectangle(output->cr,
		x - 10.0, y - 20.0,
		width + 20.0, output->extents.height + 20.0);
	cairo_fill(output->cr);

	cairo_set_source_rgb(output->cr, 0.8, 0.8, 0.8);
	cairo_move_to(output->cr, x, y - 10.0 + output->extents.ascent);
	cairo_show_text(output->cr, string);

	cairo_stroke(output->cr);
}

/*
 * show some help
 */
void helplabel(struct position *position, struct output *output) {
	(void) position;

	if (output->help[0] == '\0')
		return;

	label(output, output->help, 1);
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
void pagenumber(struct position *position, struct output *output) {
	static int prev = -1;
	char r[30], s[100];
	int hasannots, hasactions;
	char *other, *annots, *actions;

	if ((position->npage == prev || ! output->pagelabel) &&
	    ! output->pagenumber)
		return;

	if (output->first != -1 && output->last != -1)
		snprintf(r, 30, " - range:%d-%d",
			output->first + 1, output->last + 1);
	else if (output->first != -1)
		snprintf(r, 30, " - range:%d-", output->first + 1);
	else if (output->last != -1)
		snprintf(r, 30, " - range:-%d", output->last + 1);
	else
		r[0] = '\0';

	hasannots = checkannotations(position);
	hasactions = checkactions(position);
	other = hasannots || hasactions ? " - contains" : "";
	annots = hasannots ? " annotations" : "";
	actions = hasactions ? hasannots ? " and actions" : " actions" : "";

	if (output->totalpages)
		snprintf(s, 100, "page %d of %d%s%s%s%s",
			position->npage + 1, position->totpages,
			other, annots, actions, r);
	else
		sprintf(s, "page %d%s%s%s%s", position->npage + 1,
			other, annots, actions, r);
	label(output, s, 2);

	if (output->timeout == NO_TIMEOUT)
		output->timeout = 1200;
	output->pagenumber = FALSE;
	prev = position->npage;
}

/*
 * show the current mode
 */
void showmode(struct position *position, struct output *output) {
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
	label(output, s, 3);

	if (output->timeout == NO_TIMEOUT)
		output->timeout = 1200;
	output->showmode = FALSE;
	prev = output->viewmode;
}

/*
 * show the current fit direction
 */
void showfit(struct position *position, struct output *output) {
	static int prev = -1;
	char s[100];
	char *fits[] = {"none", "horizontal", "vertical", "both"};

	(void) position;

	if (output->fit == prev && ! output->showfit)
		return;

	sprintf(s, "fit: %s", fits[output->fit]);
	label(output, s, 4);

	if (output->timeout == NO_TIMEOUT)
		output->timeout = 1200;
	output->showfit = FALSE;
	prev = output->fit;
}

/*
 * show the filename
 */
void filename(struct position *position, struct output *output) {
	char s[100];

	if (! output->filename)
		return;

	sprintf(s, "%s", position->filename);
	label(output, s, 5);

	if (output->timeout == NO_TIMEOUT)
		output->timeout = 1200;
	output->filename = FALSE;
}

/*
 * draw a list of rectangles in pdf points
 */
void selection(struct position *position, struct output *output, GList *s) {
	double width, height;
	GList *l;
	PopplerRectangle *r;

	cairo_save(output->cr);
	cairo_scale(output->cr, 1, -1);
	poppler_page_get_size(position->page, &width, &height);
	cairo_translate(output->cr, 0, -height);
	cairo_set_source_rgb(output->cr, 0.3, 0.0, 0.3);
	cairo_set_operator(output->cr, CAIRO_OPERATOR_DIFFERENCE);
	for (l = s; l != NULL; l = l->next) {
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
 * draw the document with the labels on top
 */
void draw(struct cairooutput *cairo,
		void cairoclear(struct cairooutput *cairo),
		void cairoflush(struct cairooutput *cairo),
		struct position *position, struct output *output) {
	if (output->redraw) {
		cairoclear(cairo);
		moveto(position, output);
		if (! POPPLER_IS_PAGE(position->page)) {
			output->reload = TRUE;
			output->redraw = TRUE;
			return;
		}
		poppler_page_render(position->page, output->cr);
		if (changedpdf(position)) {
			output->reload = TRUE;
			output->redraw = TRUE;
			return;
		}
		if (output->drawbox) {
			rectangle_draw(output->cr,
				&position->textarea->rect[position->box],
				FALSE, FALSE, TRUE);
			pageborder(position, output);
		}
		selection(position, output, output->found);
		selection(position, output, output->selection);
		output->redraw = FALSE;
	}

	helplabel(position, output);
	pagenumber(position, output);
	showmode(position, output);
	showfit(position, output);
	filename(position, output);

	if (output->flush) {
		cairoflush(cairo);
		output->flush = FALSE;
	}
}

/*
 * resize output
 */
void resize(struct position *position, struct output *output,
		double width, double height,
		double margin, double fontsize) {
	output->dest.x1 = margin;
	output->dest.y1 = margin;
	output->dest.x2 = width - margin;
	output->dest.y2 = height - margin;

	/* set font again because a resize may have implied the destruction and
	 * recreation of the context */
	cairo_select_font_face(output->cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output->cr, fontsize);
	cairo_font_extents(output->cr, &output->extents);

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
		exit(EXIT_FAILURE);
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
struct position *reloadpdf(struct position *position, struct output *output) {
	struct position *new;

	output->reload = FALSE;

	new = openpdf(position->filename);
	if (new == NULL)
		return NULL;
	initposition(new);

	if (position->npage >= new->totpages) {
		new->npage = new->totpages - 1;
		readpage(new, output);
		lasttextbox(new, output);
		free(position);
		return new;
	}
	new->npage = position->npage;
	readpage(new, output);

	new->box = position->box >= new->textarea->num ?
		new->textarea->num - 1 :
		position->box;
	free(position);
	return new;
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
int external(int window, struct command *command,
		struct position *position, struct output *output) {
	char *newline;
	int page;
	char dest[100];

	newline = strchr(command->command, '\n');
	if (newline)
		*newline = '\0';

	if (command->command[0] == '#' || ! strcmp(command->command, "nop"))
		return window;
	if (! strcmp(command->command, "quit"))
		return WINDOW_EXIT;
	if (! strcmp(command->command, "document"))
		return WINDOW_DOCUMENT;
	if (! strcmp(command->command, "reload")) {
		output->reload = TRUE;
		return WINDOW_REFRESH;
	}
	if (1 == sscanf(command->command, "gotopage %d", &page)) {
		return movetopage(position, output, page) ?
			window : WINDOW_REFRESH;
	}
	if (1 == sscanf(command->command, "gotodestination %90s", dest)) {
		return movetonameddestination(position, output, dest) ?
			window : WINDOW_REFRESH;
	}

	printhelp(output, 4000, "error in command: %s", command->command);
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
 * log the state of the main loop
 */
#define LEVEL_MAIN  0x0001
void logstatus(int level, char *prefix, int window,
		struct output *output, int c) {
	char *levname, levnum[8];
	char *keyname, keynum[8];
	char *winname, winnum[3];

	if ((level & output->log) == 0)
		return;

	switch (level) {
	case LEVEL_MAIN:
		levname = "MAIN";
		break;
	default:
		snprintf(levname = levnum, 8, "LEVEL%d", level);
	}

	switch (c) {
	case KEY_NONE:
		keyname = "KEY_NONE";
		break;
	case KEY_INIT:
		keyname = "KEY_INIT";
		break;
	case KEY_REFRESH:
		keyname = "KEY_REFRESH";
		break;
	case KEY_REDRAW:
		keyname = "KEY_REDRAW";
		break;
	case KEY_RESIZE:
		keyname = "KEY_RESIZE";
		break;
	case KEY_TIMEOUT:
		keyname = "KEY_TIMEOUT";
		break;
	case KEY_SUSPEND:
		keyname = "KEY_SUSPEND";
		break;
	case KEY_SIGNAL:
		keyname = "KEY_SIGNAL";
		break;
	case KEY_EXTERNAL:
		keyname = "KEY_EXTERNAL";
		break;
	default:
		if (isprint(c))
			snprintf(keyname = keynum, 8, "%c", c);
		else
			snprintf(keyname = keynum, 8, "[%d]", c);
	}

	switch (window) {
	case WINDOW_DOCUMENT:
		winname = "WINDOW_DOCUMENT";
		break;
	case WINDOW_MENU:
		winname = "WINDOW_MENU";
		break;
	case WINDOW_REFRESH:
		winname = "WINDOW_REFRESH";
		break;
	case WINDOW_EXIT:
		winname = "WINDOW_EXIT";
		break;
	default:
		snprintf(winname = winnum, 3, "%d", window);
	}

	ensureoutputfile(output);
	fprintf(output->outfile, "%-5s", levname);
	fprintf(output->outfile, " %-12s", prefix);
	fprintf(output->outfile, " %-15s", winname);
	fprintf(output->outfile, " %-12s", keyname);
	fprintf(output->outfile, " timeout=%-5d", output->timeout);
	fprintf(output->outfile, " redraw=%d", output->redraw);
	fprintf(output->outfile, " flush=%d\n", output->flush);
	fflush(output->outfile);
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
void usage() {
	printf("pdf viewer with automatic zoom to text\n");
	printf("usage:\n\thovacui\t[-m viewmode] [-f direction] ");
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
	printf("main keys: 'h'=help 'g'=go to page '/'=search 'q'=quit ");
	printf("'m'=menu\n");
}

/*
 * signal handling: reload on SIGHUP
 */
int sig_reload;
void handler(int s) {
	if (s != SIGHUP)
		return;
	sig_reload = 1;
}

/*
 * show a pdf file on an arbitrary cairo device
 */
int hovacui(int argn, char *argv[], struct cairodevice *cairodevice) {
	char configfile[4096], configline[1000], s[1000];
	FILE *config;
	int i;
	double d;
	char *outdev;
	struct cairooutput *cairo;
	double margin;
	double fontsize;
	char *filename;
	struct position *position;
	struct output output;
	double screenaspect;
	int opt;
	int firstwindow;
	int noinitlabels;
	struct command command;
	int keepopen;

	int c;
	int window, next;
	int pending;

				/* defaults */

	output.viewmode = 0;
	output.totalpages = FALSE;
	output.fit = 1;
	output.minwidth = -1;
	output.distance = -1;
	output.order = 1;
	output.scroll = 1.0 / 4.0;
	output.ui = TRUE;
	output.immediate = FALSE;
	output.drawbox = TRUE;
	output.pagelabel = TRUE;
	output.log = FALSE;
	output.outname = "hovacui-out.txt";
	output.outfile = NULL;
	output.pdfout = "selection-%d.pdf";
	output.postsave = NULL;
	output.first = -1;
	output.last = -1;
	screenaspect = -1;
	firstwindow = WINDOW_TUTORIAL;
	margin = 10.0;
	fontsize = -1;
	outdev = NULL;
	noinitlabels = FALSE;
	command.fd = -1;
	command.stream = NULL;
	command.max = 4096;
	command.command = malloc(command.max);
	keepopen = -1;

				/* config file */

	snprintf(configfile, 4096, "%s/.config/hovacui/hovacui.conf",
		getenv("HOME"));
	optind = 1;
	while (-1 != (opt = getopt(argn, argv, HOVACUIOPTS)))
		if (opt == 'C') {
			snprintf(configfile, 4096, "%s", optarg);
			break;
		}

				/* read config file */

	config = fopen(configfile, "r");
	if (config != NULL) {
		while (fgets(configline, 900, config)) {
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
				screenaspect = fraction(s);
			if (sscanf(configline, "scroll %s", s) == 1)
				output.scroll = fraction(s);
			if (sscanf(configline, "fontsize %lg", &d) == 1)
				fontsize = d;
			if (sscanf(configline, "margin %lg", &d) == 1)
				margin = d;
			if (sscanf(configline, "device %s", s) == 1)
				outdev = strdup(s);
			if (sscanf(configline, "fifo %s", s) == 1)
				openfifo(s, &command, &keepopen);
			if (sscanf(configline, "outfile %s", s) == 1)
				output.outname = strdup(s);
			if (sscanf(configline, "postsave %900c", s) == 1)
				output.postsave = strdup(s);
			if (sscanf(configline, "log %d", &i) == 1)
				output.log = i;
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
				if (! strcmp(s, "noinitlabels"))
					noinitlabels = TRUE;
				if (! strcmp(s, "presentation")) {
					output.viewmode = optindex('p', "atbp");
					output.fit = optindex('b', "nhvb");
					output.ui = FALSE;
					output.drawbox = FALSE;
					output.pagelabel = FALSE;
					output.totalpages = TRUE;
					margin = 0.0;
					firstwindow = WINDOW_DOCUMENT;
					noinitlabels = TRUE;
				}
			}
		}
		fclose(config);
	}

				/* cmdline arguments */

	optind = 1;
	while (-1 != (opt = getopt(argn, argv, HOVACUIOPTS)))
		switch (opt) {
		case 'm':
			output.viewmode = optindex(optarg[0], "atbp");
			if (output.viewmode == -1) {
				printf("unsupported mode: %s\n", optarg);
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			output.fit = optindex(optarg[0], "nhvb");
			if (output.fit == -1) {
				printf("unsupported fit mode: %s\n", optarg);
				usage();
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
				usage();
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			output.viewmode = 3;
			output.fit = 1;
			output.ui = FALSE;
			output.drawbox = FALSE;
			output.pagelabel = FALSE;
			output.totalpages = TRUE;
			margin = 0.0;
			firstwindow = WINDOW_DOCUMENT;
			noinitlabels = TRUE;
			break;
		case 'd':
			outdev = optarg;
			break;
		case 's':
			screenaspect = fraction(optarg);
			break;
		case 'e':
			openfifo(optarg, &command, &keepopen);
			break;
		case 'z':
			output.outname = optarg;
			break;
		case 'l':
			output.log = atoi(optarg);
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		}

	if (argn - 1 < optind) {
		printf("file name missing\n");
		usage();
		exit(EXIT_FAILURE);
	}
	filename = argv[optind];

				/* open input file */

	position = openpdf(filename);
	if (position == NULL)
		exit(EXIT_FAILURE);

				/* signal handling */

	sig_reload = 0;
	signal(SIGHUP, handler);

				/* open fbdev as cairo */

	cairo = cairodevice->init(outdev, cairodevice->initdata);
	if (cairo == NULL) {
		cairodevice->finish(cairo);
		exit(EXIT_FAILURE);
	}

				/* initialize position and output */

	initposition(position);

	output.cr = cairodevice->context(cairo);

	resize(position, &output,
		cairodevice->width(cairo), cairodevice->height(cairo),
		margin, fontsize);

	output.screenwidth = cairodevice->screenwidth(cairo);
	output.screenheight = cairodevice->screenheight(cairo);
	output.aspect = screenaspect == -1 ?
		1 : screenaspect * output.screenheight / output.screenwidth;
	if (output.minwidth == -1)
		output.minwidth = 400;
	if (fontsize == -1)
		fontsize = output.screenheight / 25;

	strcpy(output.search, "");
	output.found = NULL;
	output.selection = NULL;
	output.texfudge = 0; // 24;
	output.paste = command.command;

	output.timeout = NO_TIMEOUT;
	output.help[0] = '\0';
	output.help[79] = '\0';

	cairo_select_font_face(output.cr, "mono",
	                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(output.cr, fontsize);
	cairo_font_extents(output.cr, &output.extents);

				/* initialize labels */

	output.redraw = FALSE;
	output.flush = FALSE;
	if (noinitlabels || firstwindow != WINDOW_DOCUMENT)
		draw(cairo, cairodevice->clear, cairodevice->flush,
			position, &output);
	else {
		output.filename = firstwindow == WINDOW_DOCUMENT;
		strncpy(output.help, "press 'h' for help", 79);
	}

				/* first window */

	readpage(position, &output);
	window = firstwindow;
	output.reload = FALSE;
	output.flush = TRUE;
	output.redraw = firstwindow == WINDOW_DOCUMENT;
	c = firstwindow == WINDOW_DOCUMENT ? KEY_NONE : KEY_INIT;
	if (checkannotations(position))
		output.pagenumber = TRUE;

				/* event loop */

	while (window != WINDOW_EXIT) {

					/* draw document and labels */

		logstatus(LEVEL_MAIN, "start", window, &output, c);
		if (output.reload || sig_reload) {
			if (sig_reload)
				output.redraw = TRUE;
			sig_reload = 0;
			position = reloadpdf(position, &output);
			c = output.redraw ? KEY_REDRAW : KEY_NONE;
		}
		if (! cairodevice->isactive(cairo))
			c = KEY_NONE;
		else if (c != KEY_INIT || output.redraw) {
			logstatus(LEVEL_MAIN, "draw", window, &output, c);
			draw(cairo, cairodevice->clear, cairodevice->flush,
				position, &output);
			if (output.reload)
				continue;
		}

					/* read input */

		logstatus(LEVEL_MAIN, "preinput", window, &output, c);
		if (c != KEY_NONE)
			pending = 0;
		else {
			pending = output.timeout != NO_TIMEOUT &&
				output.timeout != 0;
			c = cairodevice->input(cairo, output.timeout, &command);
			if (c != KEY_REDRAW)
				output.timeout = NO_TIMEOUT;
			logstatus(LEVEL_MAIN, "postinput", window, &output, c);
		}
		if (c == KEY_SUSPEND || c == KEY_SIGNAL || c == KEY_NONE) {
			c = KEY_NONE;
			continue;
		}
		if (c == KEY_RESIZE || c == KEY_REDRAW || pending) {
			if (c == KEY_RESIZE)
				resize(position, &output,
					cairodevice->width(cairo),
					cairodevice->height(cairo),
					margin, fontsize);
			output.redraw = TRUE;
			output.flush = FALSE;
			if (pending && c == KEY_TIMEOUT) {
				output.timeout = NO_TIMEOUT;
				c = KEY_REFRESH;
				continue;
			}
			if (c == KEY_RESIZE || c == KEY_REDRAW) {
				c = KEY_REFRESH;
				continue;
			}
		}

					/* pass input to window or external */

		logstatus(LEVEL_MAIN, "prewindow", window, &output, c);
		next = c == KEY_EXTERNAL ?
			external(window, &command, position, &output) :
			selectwindow(window, c, position, &output);
		logstatus(LEVEL_MAIN, "postwindow", next, &output, c);
		c = KEY_NONE;
		if (next == window)
			continue;
		if (next == WINDOW_REFRESH) {
			/* for WINDOW_DOCUMENT, redraw the page and flush; all
			 * other windows: redraw the page and call the window
			 * again with KEY_REFRESH; flush then (not now) */
			output.redraw = TRUE;
			output.flush = window == WINDOW_DOCUMENT;
			c = window == WINDOW_DOCUMENT ? KEY_NONE : KEY_REFRESH;
			continue;
		}
		if (next == WINDOW_DOCUMENT) {
			output.redraw = TRUE;
			output.flush = TRUE;
			window = next;
			continue;
		}
		if (window != WINDOW_DOCUMENT)
			output.redraw = TRUE;
		window = next;
		c = KEY_INIT;
	}

				/* close */

	closepdf(position);
	cairodevice->finish(cairo);
	close(keepopen);
	if (command.fd != -1)
		fclose(command.stream);
	if (output.outfile != NULL)
		fclose(output.outfile);
	return EXIT_SUCCESS;
}

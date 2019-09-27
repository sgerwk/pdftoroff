/*
 * cairoui.h
 */

#ifdef _CAIROUI_H
#else
#define _CAIROUI_H

/*
 * return values of windows, other than a window
 */
#define CAIROUI_DONE      -1
#define CAIROUI_LEAVE     -2
#define CAIROUI_INVALID   -3
#define CAIROUI_UNCHANGED -4
#define CAIROUI_CHANGED   -5
#define CAIROUI_REFRESH   -6
#define CAIROUI_EXIT      -7
#define CAIROUI_OUT(res) \
	((res) == CAIROUI_DONE || (res) == CAIROUI_LEAVE)

/*
 * the cairoui structure, passed to all functions
 */
struct cairoui {
	/* cairo device */
	struct cairodevice *cairodevice;

	/* cairo surface */
	cairo_t *cr;

	/* destination rectangle */
	cairo_rectangle_t dest;
	int margin;

	/* size of font */
	int fontsize;
	cairo_font_extents_t extents;

	/* whether the output is to be flushed */
	int flush;

	/* whether the document has to be redrawn */
	int redraw;

	/* whether to update/reload the document */
	int reload;

	/* if not NO_TIMEOUT, stop input on timeout and return KEY_TIMEOUT */
	int timeout;

	/* pasted text */
	char *paste;

	/* external command */
	struct command command;

	/* log file */
	int log;
	char *outname;
	FILE *outfile;

	/* callback data */
	void *cb;

	/* window list */
	struct windowlist {
		int window;
		char *name;
		int (*function)(int, struct cairoui *);
	} *windowlist;

	/* label list */
	void (**labellist)(struct cairoui *);

	/* draw the document */
	void (*draw)(struct cairoui *cairoui);

	/* resize function */
	void (*resize)(struct cairoui *cairoui);

	/* update/reload function */
	void (*update)(struct cairoui *cairoui);

	/* external command */
	int (*external)(struct cairoui *cairoui, int window);
};

/*
 * reload by signal
 */
int sig_reload;

/*
 * a list of strings, possibly with a selected one
 */
int cairoui_list(int c, struct cairoui *cairoui, char *viewtext[],
		int *line, int *selected);

/*
 * a textfield
 */
int cairoui_field(int c, struct cairoui *cairoui,
		char *prompt, char *current, int *pos, char *error);

/*
 * a textfield for an integer number
 */
int cairoui_number(int c, struct cairoui *cairoui,
		char *prompt, char *current, int *pos, char *error,
		int *destination, double min, double max);

/*
 * a label at the given number of labels from the bottom
 */
void cairoui_label(struct cairoui *cairoui, char *string, int bottom);

/*
 * formatted print to a label; timeout=NO_TIMEOUT means infinite
 */
int cairoui_printlabel(struct cairoui *cairoui, char *help, int timeout,
		char *format, ...);

/*
 * initialize the labels
 */
void cairoui_initlabels(struct cairoui *cairoui);

/*
 * cairoui structure default
 */
void cairoui_default(struct cairoui *cairoui);

/*
 * ensure the output file is open
 */
int ensureoutputfile(struct cairoui *cairoui);

/*
 * logging function
 */
#define LEVEL_MAIN  0x0001
void cairoui_logstatus(int level, char *prefix, int window,
		struct cairoui *cairoui, int c);

/*
 * main loop
 */
void cairoui_main(struct cairoui *cairoui, int firstwindow);

#endif

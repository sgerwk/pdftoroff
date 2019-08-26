/*
 * cairoui-main.c
 *
 * a test program for cairoui
 * show a square; change color with 'c', position with 'p'
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cairo.h>
#include "cairoio.h"
#include "cairoui.h"
#include "cairoio-fb.h"
#include "cairoio-x11.h"

/*************************** CALLBACK DATA ************************/

/*
 * callback data
 */
struct cairoui_cb {
	int immediate;
	char help[80];

	int color;
	int x;
	int y;
};

/******************************* WINDOWS **************************/

/*
 * window names
 */
enum window {
	WINDOW_DOCUMENT,
	WINDOW_COLOR,
	WINDOW_POSITION
};

/*
 * document
 */
int document(int c, struct cairoui *cairoui) {
	(void) cairoui;
	switch (c) {
	case 'c':
		return WINDOW_COLOR;
	case 'p':
		return WINDOW_POSITION;
	case 'q':
		return CAIROUI_EXIT;
	default:
		return WINDOW_DOCUMENT;
	}
}

/*
 * color
 */
int color(int c, struct cairoui *cairoui) {
	struct cairoui_cb *cairoui_cb = cairoui->cb;
	static char *colortext[] = {
		"select color",
		"red",
		"green",
		"blue",
		NULL
	};
	static int line = 0;
	static int selected = 1;
	int res;

	if (c == KEY_INIT)
		selected = cairoui_cb->color + 1;

	res = cairoui_list(c, cairoui, colortext, &line, &selected);
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	if (res != CAIROUI_DONE)
		return WINDOW_COLOR;
	switch (selected) {
	case 1:
	case 2:
	case 3:
		cairoui_cb->color = selected - 1;
		break;
	}
	return cairoui_cb->immediate ? CAIROUI_REFRESH : WINDOW_DOCUMENT;
}

/*
 * position
 */
int position(int c, struct cairoui *cairoui) {
	struct cairoui_cb *cairoui_cb = cairoui->cb;
	static char positionstring[100] = "";
	static int pos = 0;
	int res;

	res = cairoui_number(c, cairoui,
		"position: ", positionstring, &pos, NULL,
		&cairoui_cb->x, 0, 200);
	cairoui_cb->y = cairoui_cb->x;
	if (res == CAIROUI_DONE)
		return cairoui_cb->immediate ?
			CAIROUI_REFRESH : WINDOW_DOCUMENT;
	if (res == CAIROUI_LEAVE)
		return WINDOW_DOCUMENT;
	cairoui_printlabel(cairoui, cairoui_cb->help,
		NO_TIMEOUT, "down=increase up=decrease");
	return WINDOW_POSITION;
}

/*
 * window list
 */
struct windowlist windowlist[] = {
{	WINDOW_DOCUMENT,	"DOCUMENT",	document	}, // always 0
{	WINDOW_COLOR,		"COLOR",	color		},
{	WINDOW_POSITION,	"POSITION",	position	},
{	0,			NULL,		NULL		}
};

/***************************** LABELS ******************************/

/*
 * help label
 */
void helplabel(struct cairoui *cairoui) {
	struct cairoui_cb *cairoui_cb = cairoui->cb;

	if (cairoui_cb->help[0] == '\0')
		return;

	cairoui_label(cairoui, cairoui_cb->help, 1);
	cairoui_cb->help[0] = '\0';
}

/*
 * list of labels
 */
void (*labellist[])(struct cairoui *) = {
	helplabel,
	NULL
};

/************************ CAIROUI CALLBACKS ************************/

/*
 * draw
 */
void draw(struct cairoui *cairoui) {
	struct cairoui_cb *cairoui_cb = cairoui->cb;

	cairo_identity_matrix(cairoui->cr);
	switch (cairoui_cb->color) {
	case 0:
		cairo_set_source_rgb(cairoui->cr, 1.0, 0.0, 0.0);
		break;
	case 1:
		cairo_set_source_rgb(cairoui->cr, 0.0, 1.0, 0.0);
		break;
	case 2:
		cairo_set_source_rgb(cairoui->cr, 0.0, 0.0, 1.0);
		break;
	}
	cairo_rectangle(cairoui->cr, cairoui_cb->x, cairoui_cb->y, 100, 100);
	cairo_fill(cairoui->cr);
}

/******************************* MAIN ***************************/

/*
 * main
 */
int main(int argn, char *argv[]) {
	char *mainopts = "h", *allopts;
	char *outdev = NULL;
	int doublebuffering = 1;
	int canopen;
	struct cairoui_cb cairoui_cb;
	struct cairoui cairoui;
	int firstwindow = WINDOW_DOCUMENT;

				/* select the cairo device */

	cairoui.cairodevice =
		getenv("DISPLAY") ? &cairodevicex11 : &cairodevicefb;

				/* merge general and device-specific options */

	allopts = malloc(strlen(mainopts) +
		strlen(cairoui.cairodevice->options) + 1);
	strcpy(allopts, mainopts);
	strcat(allopts, cairoui.cairodevice->options);

				/* open output device as cairo */

	canopen = cairoui.cairodevice->init(cairoui.cairodevice,
		outdev, doublebuffering, argn, argv, allopts);
	if (canopen == -1) {
		cairoui.cairodevice->finish(cairoui.cairodevice);
		exit(EXIT_FAILURE);
	}
	free(allopts);

				/* initialize the cairo user interface */

	cairoui_default(&cairoui);
	cairoui.cb = &cairoui_cb;

	cairoui_cb.immediate = TRUE;
	cairoui_cb.help[0] = '\0';
	cairoui_cb.color = 1;
	cairoui_cb.x = 10;
	cairoui_cb.y = 10;

	cairoui.draw = draw;
	cairoui.windowlist = windowlist;
	cairoui.labellist = labellist;

	cairoui.log = LEVEL_MAIN;

	// cairoui_cb.immediate = FALSE;
	// firstwindow = WINDOW_COLOR;

				/* event loop */

	cairoui_main(&cairoui, firstwindow);

	return EXIT_SUCCESS;
}


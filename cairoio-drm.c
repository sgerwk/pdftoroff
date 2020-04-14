/*
 * cairoio-drm.c
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "vt.h"
#include "cairodrm.h"
#include "cairoio.h"
#include "cairoio-drm.h"

/*
 * structure for init data
 */
struct initdata {
};

/*
 * check whether b is a prefix of a
 */
int _cairodrm_prefix(char *a, char *b) {
	return strncmp(a, b, strlen(b));
}

/*
 * extract the last part of a string
 */
char *_cairodrm_second(char *a) {
	char *p;
	p = index(a, '=');
	return p == NULL ? p : p + 1;
}

/*
 * create a cairo context
 */
int cairoinit_drm(struct cairodevice *cairodevice,
		char *device, int doublebuffering,
		int argn, char *argv[], char *allopts) {
	struct cairodrm *cairodrm;
	int opt;
	char *connectors;
	WINDOW *w;

	(void) argn;
	(void) argv;
	(void) allopts;

	if (device == NULL)
		device = "/dev/dri/card0";

	connectors = "all";
	optind = 1;
	while (-1 != (opt = getopt(argn, argv, allopts))) {
		switch (opt) {
		case 'r':
			if (! strcmp(optarg, "default"))
				connectors = "all";
			else if (! strcmp(optarg, "all"))
				connectors = "all";
			else if (! _cairodrm_prefix(optarg, "connectors="))
				connectors = _cairodrm_second(optarg);
			else {
				printf("unknown -r suboption: %s\n", optarg);
				return -1;
			}
			break;
		}
	}

	cairodrm = cairodrm_init(device, doublebuffering, connectors);
	if (cairodrm == NULL) {
		if (! ! strcmp(connectors, "list"))
			printf("cannot open %s as a cairo surface\n", device);
		return -1;
	}

	if (getenv("ESCDELAY") == NULL)
		setenv("ESCDELAY", "200", 1);
	w = initscr();
	cbreak();
	noecho();
	keypad(w, TRUE);
	curs_set(0);
	ungetch(KEY_INIT);
	getch();
	timeout(0);

	vt_setup();

	cairodevice->cairoio = (struct cairoio *) cairodrm;
	return 0;
}

/*
 * close a cairo context
 */
void cairofinish_drm(struct cairodevice *cairodevice) {
	if (cairodevice != NULL && cairodevice->cairoio != NULL)
		cairodrm_finish((struct cairodrm *) cairodevice->cairoio);
	clear();
	refresh();
	endwin();
}

/*
 * get the cairo context from a cairo envelope
 */
cairo_t *cairocontext_drm(struct cairodevice *cairodevice) {
	return ((struct cairodrm *) cairodevice->cairoio)->cr;
}

/*
 * get the width from a cairo envelope
 */
double cairowidth_drm(struct cairodevice *cairodevice) {
	return ((struct cairodrm *) cairodevice->cairoio)->width;
}

/*
 * get the heigth from a cairo envelope
 */
double cairoheight_drm(struct cairodevice *cairodevice) {
	return ((struct cairodrm *) cairodevice->cairoio)->height;
}

/*
 * return whether double buffering is used
 */
int cairodoublebuffering_drm(struct cairodevice *cairodevice) {
	return cairodrm_doublebuffering(
		(struct cairodrm *) cairodevice->cairoio);
}

/*
 * clear
 */
void cairoclear_drm(struct cairodevice *cairodevice) {
	cairodrm_clear((struct cairodrm *) cairodevice->cairoio, 1.0, 1.0, 1.0);
}

/*
 * blank
 */
void cairoblank_drm(struct cairodevice *cairodevice) {
	cairodrm_clear((struct cairodrm *) cairodevice->cairoio, 0.0, 0.0, 0.0);
}

/*
 * flush
 */
void cairoflush_drm(struct cairodevice *cairodevice) {
	cairodrm_flush((struct cairodrm *) cairodevice->cairoio);
}

/*
 * whether the output is currently active
 */
int cairoisactive_drm(struct cairodevice *cairodevice) {
	(void) cairodevice;
	return ! vt_suspend;
}

/*
 * get a single input from a cairo envelope
 */
int cairoinput_drm(struct cairodevice *cairodevice, int timeout,
		struct command *command) {
	fd_set fds;
	int max, ret;
	struct timeval tv;
	int c, l, r;

	(void) cairodevice;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	max = STDIN_FILENO;
	if (command->fd != -1) {
		FD_SET(command->fd, &fds);
		max = max > command->fd ? max : command->fd;
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;

	ret = select(max + 1, &fds, NULL, NULL,
		timeout != NO_TIMEOUT ? &tv : NULL);
	if (ret != -1 && command->fd != -1 && FD_ISSET(command->fd, &fds)) {
		fgets(command->command, command->max, command->stream);
		return KEY_EXTERNAL;
	}

	if (vt_suspend && timeout != 0)
		return KEY_SUSPEND;

	if (vt_redraw) {
		vt_redraw = FALSE;
		return KEY_REDRAW;
	}

	if (ret == -1)
		return KEY_SIGNAL;

	if (FD_ISSET(STDIN_FILENO, &fds)) {
		for (l = getch(), r = 0;
		     l != ERR && r < command->max - 1;
		     l = getch()) {
			command->command[r++] = l;
			c = l;
		}
		command->command[r] = '\0';
		return r < 4 ? c : KEY_PASTE;
	}

	return KEY_TIMEOUT;
}

/*
 * the cairo device for the framebuffer
 */
struct cairodevice cairodevicedrm = {
	"r:",
	NULL,
	cairoinit_drm, cairofinish_drm,
	cairocontext_drm,
	cairowidth_drm, cairoheight_drm,
	cairowidth_drm, cairoheight_drm,
	cairodoublebuffering_drm,
	cairoclear_drm, cairoblank_drm, cairoflush_drm,
	cairoisactive_drm, cairoinput_drm
};


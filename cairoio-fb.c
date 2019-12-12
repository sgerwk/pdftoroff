#include <stdlib.h>
#include <unistd.h>
#include "vt.h"
#include "cairofb.h"
#include "cairoio.h"
#include "cairoio-fb.h"

/*
 * structure for init data
 */
struct initdata {
};

/*
 * create a cairo context
 */
int cairoinit_fb(struct cairodevice *cairodevice,
		char *device, int doublebuffering,
		int argn, char *argv[], char *allopts) {
	struct cairofb *cairofb;
	WINDOW *w;

	(void) argn;
	(void) argv;
	(void) allopts;

	if (device == NULL)
		device = "/dev/fb0";

	cairofb = cairofb_init(device, doublebuffering);
	if (cairofb == NULL) {
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

	cairodevice->cairoio = (struct cairoio *) cairofb;
	return 0;
}

/*
 * close a cairo context
 */
void cairofinish_fb(struct cairodevice *cairodevice) {
	if (cairodevice != NULL && cairodevice->cairoio != NULL)
		cairofb_finish((struct cairofb *) cairodevice->cairoio);
	clear();
	refresh();
	endwin();
}

/*
 * get the cairo context from a cairo envelope
 */
cairo_t *cairocontext_fb(struct cairodevice *cairodevice) {
	return ((struct cairofb *) cairodevice->cairoio)->cr;
}

/*
 * get the width from a cairo envelope
 */
double cairowidth_fb(struct cairodevice *cairodevice) {
	return ((struct cairofb *) cairodevice->cairoio)->width;
}

/*
 * get the heigth from a cairo envelope
 */
double cairoheight_fb(struct cairodevice *cairodevice) {
	return ((struct cairofb *) cairodevice->cairoio)->height;
}

/*
 * return whether double buffering is used
 */
int cairodoublebuffering_fb(struct cairodevice *cairodevice) {
	return cairofb_doublebuffering((struct cairofb *) cairodevice->cairoio);
}

/*
 * clear
 */
void cairoclear_fb(struct cairodevice *cairodevice) {
	cairofb_clear((struct cairofb *) cairodevice->cairoio, 1.0, 1.0, 1.0);
}

/*
 * blank
 */
void cairoblank_fb(struct cairodevice *cairodevice) {
	cairofb_clear((struct cairofb *) cairodevice->cairoio, 0.0, 0.0, 0.0);
}

/*
 * flush
 */
void cairoflush_fb(struct cairodevice *cairodevice) {
	cairofb_flush((struct cairofb *) cairodevice->cairoio);
}

/*
 * whether the output is currently active
 */
int cairoisactive_fb(struct cairodevice *cairodevice) {
	(void) cairodevice;
	return ! vt_suspend;
}

/*
 * get a single input from a cairo envelope
 */
int cairoinput_fb(struct cairodevice *cairodevice, int timeout,
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
struct cairodevice cairodevicefb = {
	"",
	NULL,
	cairoinit_fb, cairofinish_fb,
	cairocontext_fb,
	cairowidth_fb, cairoheight_fb,
	cairowidth_fb, cairoheight_fb,
	cairodoublebuffering_fb,
	cairoclear_fb, cairoblank_fb, cairoflush_fb,
	cairoisactive_fb, cairoinput_fb
};


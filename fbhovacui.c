#include <stdlib.h>
#include <unistd.h>
#include <ncurses.h>
#include "vt.h"
#include "cairofb.h"
#include "hovacui.h"

/*
 * create a cairo context
 */
void *cairoinit_fb(char *device) {
	struct cairofb *cairofb;
	WINDOW *w;

	cairofb = cairofb_init(device, 1);
	if (cairofb == NULL) {
		printf("cannot open %s as a cairo surface\n", device);
		return cairofb;
	}

	if (getenv("ESCDELAY") == NULL)
		setenv("ESCDELAY", "200", 1);
	w = initscr();
	cbreak();
	keypad(w, TRUE);
	noecho();
	curs_set(0);
	ungetch(KEY_INIT);
	getch();
	timeout(0);

	vt_setup();

	return cairofb;
}

/*
 * close a cairo context
 */
void cairofinish_fb(void *cairo) {
	if (cairo != NULL)
		cairofb_finish((struct cairofb *)cairo);
	clear();
	refresh();
	endwin();
}

/*
 * get the cairo context from a cairo envelope
 */
cairo_t *cairocontext_fb(void *cairo) {
	return ((struct cairofb *) cairo)->cr;
}

/*
 * get the width from a cairo envelope
 */
double cairowidth_fb(void *cairo) {
	return ((struct cairofb *) cairo)->width;
}

/*
 * get the heigth from a cairo envelope
 */
double cairoheight_fb(void *cairo) {
	return ((struct cairofb *) cairo)->height;
}

/*
 * clear a cairo envelope
 */
void cairoclear_fb(void *cairo) {
	cairofb_clear((struct cairofb *) cairo, 1.0, 1.0, 1.0);
}

/*
 * flush a cairo envelope
 */
void cairoflush_fb(void *cairo) {
	cairofb_flush((struct cairofb *) cairo);
}

/*
 * get a single input from a cairo envelope
 */
int cairoinput_fb(void *cairo, int timeout) {
	fd_set fds;
	int max, ret;
	struct timeval tv;
	int c, l;

	(void) cairo;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	max = STDIN_FILENO;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;

	ret = select(max + 1, &fds, NULL, NULL, timeout != 0 ? &tv : NULL);

	if (vt_suspend)
		return KEY_SUSPEND;

	if (ret == -1) {
		if (vt_redraw) {
			vt_redraw = FALSE;
			return KEY_REDRAW;
		}
		else
			return KEY_SIGNAL;
	}

	if (FD_ISSET(STDIN_FILENO, &fds)) {
		for (l = ' '; l != ERR; l = getch())
			c = l;
		return c;
	}

	return KEY_TIMEOUT;
}

/*
 * main
 */
#ifdef NOMAIN
#else
int main(int argn, char *argv[]) {
	struct cairodevice cairodevice = {
		cairoinit_fb, cairofinish_fb,
		cairocontext_fb,
		cairowidth_fb, cairoheight_fb,
		cairowidth_fb, cairoheight_fb,
		cairoclear_fb, cairoflush_fb, cairoinput_fb
	};

	return hovacui(argn, argv, &cairodevice);
}
#endif

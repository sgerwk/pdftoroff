/*
 * vt.c
 *
 * vt switch handling
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/vt.h>

/*
 * whether the terminal is switched out or not
 */
volatile int vt_suspend;

/*
 * terminal needs redrawing
 */
volatile int vt_redraw;

/*
 * called when the terminal switches in (1) or out (0)
 */
void *switcherdata;
void (*switcher)(int, void *);
void noswitcher(int inout, void *data) {
	(void) inout;
	(void) data;
}

/*
 * signal handler: leave the virtual terminal
 */
void sigusr1(int s) {
	(void) s;
	switcher(0, switcherdata);
	ioctl(STDIN_FILENO, VT_RELDISP, 1);
	vt_suspend = 1;
}

/*
 * signal handler: enter the virtual terminal
 */
void sigusr2(int s) {
	(void) s;
	switcher(1, switcherdata);
	ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
	vt_suspend = 0;
	vt_redraw = 1;
}

/*
 * setup virtual terminal for suspend and resume
 */
void vt_setup(void (*switcherfunction)(int, void *), void *data) {
	struct vt_mode vtmode;
	int ttyfd;
	int res;

	switcher = switcherfunction ? switcherfunction : noswitcher;
	switcherdata = data;

	vt_suspend = 0;
	vt_redraw = 0;
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr2);

	ttyfd = STDIN_FILENO;

	res = ioctl(ttyfd, VT_GETMODE, &vtmode);
	if (res == -1) {
		perror("VT_GETMODE");
		return;
	}
	vtmode.mode = VT_PROCESS;
	vtmode.relsig = SIGUSR1;
	vtmode.acqsig = SIGUSR2;
	res = ioctl(ttyfd, VT_SETMODE, &vtmode);
	if (res == -1) {
		perror("VT_SETMODE");
		return;
	}

	// tell the kernel not to restore the text on page
	// uncomment when program is finished
	// res = ioctl(tty, KDSETMODE, KD_GRAPHICS);
	// check res
}


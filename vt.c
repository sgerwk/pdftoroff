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
 * signal handler: leave the virtual terminal
 */
void sigusr1(int s) {
	(void) s;
	ioctl(STDIN_FILENO, VT_RELDISP, 1);
	vt_suspend = 1;
}

/*
 * signal handler: enter the virtual terminal
 */
void sigusr2(int s) {
	(void) s;
	ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
	vt_suspend = 0;
	vt_redraw = 1;
}

/*
 * setup virtual terminal for suspend and resume
 */
void vt_setup() {
	struct vt_mode vtmode;

	vt_suspend = 0;
	vt_redraw = 0;
	signal(SIGUSR1, sigusr1);
	signal(SIGUSR2, sigusr2);

	ioctl(STDIN_FILENO, VT_GETMODE, &vtmode);
	vtmode.mode = VT_PROCESS;
	vtmode.relsig = SIGUSR1;
	vtmode.acqsig = SIGUSR2;
	ioctl(STDIN_FILENO, VT_SETMODE, &vtmode);

	// tell the kernel not to restore the text on page
	// uncomment when program is finished
	// ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS);
}


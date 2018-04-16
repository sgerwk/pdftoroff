#include <cairo.h>
#include "hovacui.h"

#define NOMAIN
#include "fbhovacui.c"
#include "xhovacui.c"

int main(int argn, char *argv[]) {
	int x11 = 0, opt;

	while (-1 != (opt = getopt(argn, argv, HOVACUIOPTS "x:"))) {
		if (opt == 'x')
			x11 = 1;
	}

	if (x11 || getenv("DISPLAY")) {
		setinitdata(argn, argv, &cairodevicex11);
		return hovacui(argn, argv, &cairodevicex11);
	}
	else
		return hovacui(argn, argv, &cairodevicefb);
}

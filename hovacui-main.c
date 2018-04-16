#include <cairo.h>
#include "hovacui.h"

#define NOMAIN
#include "fbhovacui.c"
#include "xhovacui.c"

int main(int argn, char *argv[]) {
	if (getenv("DISPLAY")) {
		setx11title(argn, argv, &cairodevicex11);
		return hovacui(argn, argv, &cairodevicex11);
	}
	else
		return hovacui(argn, argv, &cairodevicefb);
}

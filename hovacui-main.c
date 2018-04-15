#include <cairo.h>
#include "hovacui.h"

#define NOMAIN
#include "fbhovacui.c"
#include "xhovacui.c"

int main(int argn, char *argv[]) {
	struct cairodevice cairodevicefb = {
		cairoinit_fb, cairofinish_fb,
		cairocontext_fb,
		cairowidth_fb, cairoheight_fb,
		cairowidth_fb, cairoheight_fb,
		cairoclear_fb, cairoflush_fb, cairoinput_fb
	};
	struct cairodevice cairodevicex11 = {
		cairoinit_x11, cairofinish_x11,
		cairocontext_x11,
		cairowidth_x11, cairoheight_x11,
		cairoscreenwidth_x11, cairoscreenheight_x11,
		cairoclear_x11, cairoflush_x11, cairoinput_x11
	};

	if (getenv("DISPLAY"))
		return hovacui(argn, argv, &cairodevicex11);
	else
		return hovacui(argn, argv, &cairodevicefb);
}

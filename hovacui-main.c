#include <stdlib.h>
#include <unistd.h>
#include "cairoio.h"
#include "cairoio-fb.h"
#include "cairoio-x11.h"
#include "hovacui.h"

int main(int argn, char *argv[]) {
	int x11 = 0, opt;

	while (-1 != (opt = getopt(argn, argv, cairodevicex11.options)))
		if (opt != '?')
			x11 = 1;

	if (x11 || getenv("DISPLAY"))
		return hovacui(argn, argv, &cairodevicex11);
	else
		return hovacui(argn, argv, &cairodevicefb);
}

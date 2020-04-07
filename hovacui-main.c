#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "cairoio.h"
#include "cairoio-fb.h"
#include "cairoio-drm.h"
#include "cairoio-x11.h"
#include "hovacui.h"

int main(int argn, char *argv[]) {
	int opt;
	struct cairodevice *cairodevice;

	cairodevice = &cairodevicefb;
	if (getenv("DISPLAY"))
		cairodevice = &cairodevicex11;

	opterr = 0;

	optind = 1;
	while (-1 != (opt = getopt(argn, argv, cairodevicedrm.options)))
		if (opt != '?')
			cairodevice = &cairodevicedrm;

	optind = 1;
	while (-1 != (opt = getopt(argn, argv, cairodevicex11.options)))
		if (opt != '?')
			cairodevice = &cairodevicex11;

	opterr = 1;
	optind = 1;
	return hovacui(argn, argv, cairodevice);
}

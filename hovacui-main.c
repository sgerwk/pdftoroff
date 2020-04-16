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
	int cargn;
	char **cargv;

	cairodevice = &cairodevicefb;
	if (getenv("DISPLAY"))
		cairodevice = &cairodevicex11;

	opterr = 0;
	cargv = malloc(argn * sizeof(char *));

	optind = 1;
	cargn = argn;
	memcpy(cargv, argv, argn * sizeof(char *));
	while (-1 != (opt = getopt(cargn, cargv, cairodevicedrm.options)))
		if (opt != '?')
			cairodevice = &cairodevicedrm;

	optind = 1;
	cargn = argn;
	memcpy(cargv, argv, argn * sizeof(char *));
	while (-1 != (opt = getopt(cargn, cargv, cairodevicex11.options)))
		if (opt != '?')
			cairodevice = &cairodevicex11;

	free(cargv);

	opterr = 1;
	optind = 1;
	return hovacui(argn, argv, cairodevice);
}

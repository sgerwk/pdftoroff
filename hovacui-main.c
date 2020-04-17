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
	char *usage;

					/* collect usage of output devices */

	usage = malloc(strlen(cairodevicefb.usage) + 1 +
	               strlen(cairodevicedrm.usage) + 1 +
	               strlen(cairodevicex11.usage) + 1);
	strcpy(usage, "");
	// strcat(usage, cairodevicefb.usage);
	// strcat(usage, "\n");
	strcat(usage, cairodevicedrm.usage);
	strcat(usage, "\n");
	strcat(usage, cairodevicex11.usage);

					/* determine device */

	cairodevice = NULL;
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

	if (cairodevice == NULL) {
		cairodevice = &cairodevicefb;
		cairodevice->usage = usage;
	}

					/* run hovacui */

	opterr = 1;
	optind = 1;
	return hovacui(argn, argv, cairodevice);
}

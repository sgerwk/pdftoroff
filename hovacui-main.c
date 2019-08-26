#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "cairoio.h"
#include "cairoio-fb.h"
#include "cairoio-x11.h"
#include "hovacui.h"

int main(int argn, char *argv[]) {
	int x11 = 0;
	int opt;
	char **argvv;

	if (getenv("DISPLAY"))
		x11 = 1;

	argvv = malloc(sizeof(char *) * argn);
	memcpy(argvv, argv, sizeof(char *) * argn);
	opterr = 0;
	while (-1 != (opt = getopt(argn, argvv, cairodevicex11.options)))
		if (opt != '?')
			x11 = 1;
	opterr = 1;
	free(argvv);

	return hovacui(argn, argv, x11 ? &cairodevicex11 : &cairodevicefb);
}

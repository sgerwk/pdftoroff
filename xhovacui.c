#include "cairoio-x11.h"
#include "hovacui.h"

/*
 * main
 */
int main(int argn, char *argv[]) {
	if (setinitdata(argn, argv, HOVACUIOPTS, &cairodevicex11))
		return -1;
	return hovacui(argn, argv, &cairodevicex11);
}

#include "cairoio-x11.h"
#include "hovacui.h"

/*
 * main
 */
int main(int argn, char *argv[]) {
	return hovacui(argn, argv, &cairodevicex11);
}

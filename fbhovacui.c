#include "cairoio-fb.h"
#include "hovacui.h"

/*
 * main
 */
int main(int argn, char *argv[]) {
	return hovacui(argn, argv, &cairodevicefb);
}

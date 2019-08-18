/*
 * cairoio-x11.h
 */

#ifdef _CAIROIO_X11
#else
#define _CAIROIO_X11

/*
 * the cairodevice for x11
 */
struct cairodevice cairodevicex11;

/*
 * parse the x11 cmdline option -x
 */
int setinitdata(int argn, char *argv[], char *mainopts,
		struct cairodevice *cairodevice);

#endif


/*
 * hovacui.h
 */

#ifdef _HOVACUI_H
#else
#define _HOVACUI_H

#include "cairoio.h"

/*
 * the options parsed by hovacui itself
 */
#define HOVACUIOPTS "m:f:w:t:o:d:s:pe:z:l:c:C:h"

/*
 * show a pdf file on an arbitrary cairo device
 */
int hovacui(int argn, char *argv[], struct cairodevice *cairodevice);

#endif


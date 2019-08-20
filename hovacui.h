/*
 * hovacui.h
 */

#ifdef _HOVACUI_H
#else
#define _HOVACUI_H

#include "cairoio.h"

/*
 * show a pdf file on an arbitrary cairo device
 */
int hovacui(int argn, char *argv[], struct cairodevice *cairodevice);

#endif


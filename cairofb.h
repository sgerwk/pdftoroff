/* 
 * cairofb.h
 *
 * a cairo context for drawing in a linux framebuffer
 */

#ifdef _CAIROFB_H
#else
#define _CAIROFB_H

#include <cairo.h>

struct cairofb {
/* public */
	cairo_surface_t *surface;
	cairo_t *cr;
	int width;
	int height;

/* private */
	int dev;
	unsigned char *img;
	unsigned char *dbuf;
	int length;
};

struct cairofb *cairofb_init(char *devname, int doublebuffering);
void cairofb_clear(struct cairofb *cairofb,
	double red, double green, double blue);
void cairofb_flush(struct cairofb *cairofb);
void cairofb_finish(struct cairofb *cairo);

#endif


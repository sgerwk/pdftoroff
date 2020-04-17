/*
 * cairodrm.h
 *
 * a cairo context for drawing on linux drm
 */

#ifdef _CAIRODRM_H
#else
#define _CAIRODRM_H

#include <cairo.h>

#define CAIRODRM_DOUBLEBUFFERING 0x0001
#define CAIRODRM_EXACT 0x0002

struct cairodrm {
/* public */
	cairo_surface_t *surface;
	cairo_t *cr;
	int width;
	int height;

/* private */
	int dev;
	int handle;
	int buf_id;
	void *img;
	void *dbuf;
	int size;
};

struct cairodrm *cairodrm_init(char *devname,
	char *connectors, char *size, int flags);
void cairodrm_clear(struct cairodrm *cairofb,
	double red, double green, double blue);
int cairodrm_doublebuffering(struct cairodrm *cairofb);
void cairodrm_flush(struct cairodrm *cairodrm);
void cairodrm_finish(struct cairodrm *cairo);

#endif

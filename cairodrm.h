/*
 * cairodrm.h
 *
 * a cairo context for drawing on the linux direct rendering infrastructure
 */

#ifdef _CAIRODRM_H
#else
#define _CAIRODRM_H

#include <cairo.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define CAIRODRM_DOUBLEBUFFERING 0x0001
#define CAIRODRM_EXACT           0x0002

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

	drmModeResPtr resptr;
	int *enabled;
	drmModeCrtcPtr *curr, *prev;
};

struct cairodrm *cairodrm_init(char *devname,
	char *connectors, char *size, int flags);
void cairodrm_switcher(struct cairodrm *cairodrm, int inout);
void cairodrm_clear(struct cairodrm *cairofb,
	double red, double green, double blue);
int cairodrm_doublebuffering(struct cairodrm *cairofb);
void cairodrm_flush(struct cairodrm *cairodrm);
void cairodrm_finish(struct cairodrm *cairo);

#endif

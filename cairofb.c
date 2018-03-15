/*
 * cairofb.c
 *
 * a cairo context for drawing on a linux framebuffer
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include "cairofb.h"

/*
 * create a cairo context from a framebuffer device
 */
struct cairofb *cairofb_init(char *devname, int doublebuffering) {
	struct cairofb *cairofb;
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
	int res;
	int stride;
	cairo_format_t format;
	cairo_status_t status;

	cairofb = malloc(sizeof(struct cairofb));

					/* open device, get info */

	cairofb->dev = open(devname, O_RDWR);
	if (cairofb->dev == -1) {
		perror(devname);
		free(cairofb);
		return NULL;
	}

	res = ioctl(cairofb->dev, FBIOGET_FSCREENINFO, &finfo);
	if (res == -1) {
		perror("FBIOGET_FSCREENINFO");
		free(cairofb);
		return NULL;
	}
	res = ioctl(cairofb->dev, FBIOGET_VSCREENINFO, &vinfo);
	if (res == -1) {
		perror("FBIOGET_VSCREENINFO");
		free(cairofb);
		return NULL;
	}

	cairofb->width = vinfo.xres;
	cairofb->height = vinfo.yres;
	stride = finfo.line_length;
	cairofb->length = finfo.smem_len;
	format = CAIRO_FORMAT_INVALID;
	if (finfo.type == FB_TYPE_PACKED_PIXELS &&
	    finfo.visual == FB_VISUAL_TRUECOLOR) {
		if (vinfo.bits_per_pixel == 16)
			format = CAIRO_FORMAT_RGB16_565;
		if (vinfo.bits_per_pixel == 32)
			format = CAIRO_FORMAT_RGB24;
		/* TBD: also check grayscale, offset/length */
		/* TBF: if bpp, grayscale or offset/length are not supported by
		        cairo, try changing vinfo by FBIOPUT_VSCREENINFO */
	}
	if (format == CAIRO_FORMAT_INVALID) {
		printf("ERROR: unsupported type/visual\n");
		free(cairofb);
		return NULL;
	}

					/* from framebuffer to cairo */

	cairofb->img = mmap(0, cairofb->length,
	           PROT_READ | PROT_WRITE, MAP_SHARED, cairofb->dev, 0);
	if (cairofb->img == MAP_FAILED) {
		perror("mmap");
		free(cairofb);
		return NULL;
	}
	cairofb->dbuf = doublebuffering ?
		malloc(cairofb->length) : cairofb->img;

	cairofb->surface = cairo_image_surface_create_for_data(cairofb->dbuf,
	                 format, cairofb->width, cairofb->height, stride);
	status = cairo_surface_status(cairofb->surface);
	if (status != CAIRO_STATUS_SUCCESS)
		printf("WARNING: cairo status=%d\n", status);
	cairofb->cr = cairo_create(cairofb->surface);

					/* set cairo clip */

	cairo_rectangle(cairofb->cr, 0, 0, cairofb->width, cairofb->height);
	cairo_clip(cairofb->cr);

	return cairofb;
}

/*
 * clear the cairo context
 */
void cairofb_clear(struct cairofb *cairofb,
		double red, double green, double blue) {
	cairo_identity_matrix(cairofb->cr);
	cairo_set_source_rgb(cairofb->cr, red, green, blue);
	cairo_rectangle(cairofb->cr, 0, 0, cairofb->width, cairofb->height);
	cairo_fill(cairofb->cr);
}

/*
 * flush output, if double buffering
 */
void cairofb_flush(struct cairofb *cairofb) {
	if (cairofb->img != cairofb->dbuf)
		memcpy(cairofb->img, cairofb->dbuf, cairofb->length);
}

/*
 * deallocate the cairo context and related data
 */
void cairofb_finish(struct cairofb *cairofb) {
	cairo_destroy(cairofb->cr);
	cairo_surface_destroy(cairofb->surface);
	if (cairofb->img != cairofb->dbuf)
		free(cairofb->dbuf);
	munmap(cairofb->img, cairofb->length);
	close(cairofb->dev);
	free(cairofb);
}


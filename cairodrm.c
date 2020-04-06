/*
 * cairodrm.c
 *
 * a cairo context for drawing on a linux framebuffer
 *
 * to do:
 * - choice of connector(s) and modes
 * - multiple connectors (clone output)
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <cairo.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "cairodrm.h"

/*
 * create a cairo context from a drm device
 */
struct cairodrm *cairodrm_init(char *devname, int doublebuffering) {
	int drm, res;
	uint64_t supportdumb;
	drmModeResPtr resptr;

	drmModeConnectorPtr conn;
	drmModeEncoderPtr enc;
	uint32_t crtc;
	drmModeModeInfoPtr mode;
	int nmode = 0;

	struct drm_mode_create_dumb createdumb;
	struct drm_mode_map_dumb mapdumb;

	int i;
	uint32_t buf_id;

	unsigned char *img, *dbuf;
	int width, height, stride;
	cairo_format_t format;
	cairo_surface_t *surface;
	cairo_status_t status;
	cairo_t *cr;

	struct cairodrm *cairodrm;

				/* check availability */

	res = drmAvailable();
	if (res != 1) {
		printf("drm not available\n");
		return NULL;
	}

				/* open card */

	drm = open(devname, O_RDWR);
	if (drm == -1) {
		perror(devname);
		return NULL;
	}

	res = drmGetCap(drm, DRM_CAP_DUMB_BUFFER, &supportdumb);
	// tbd: check res and supportdumb

				/* list thinghs */

	resptr = drmModeGetResources(drm);
	printf("count_fbs: %d\n", resptr->count_fbs);
	for (i = 0; i < resptr->count_fbs; i++)
		printf("fb[%d] = %d\n", i, resptr->crtcs[i]);
	printf("count_crtcs: %d\n", resptr->count_crtcs);
	for (i = 0; i < resptr->count_crtcs; i++)
		printf("crtc[%d] = %d\n", i, resptr->crtcs[i]);
	printf("count_connectors: %d\n", resptr->count_connectors);
	for (i = 0; i < resptr->count_connectors; i++)
		printf("connector[%d] = %d\n", i, resptr->connectors[i]);
	printf("count_encoders: %d\n", resptr->count_encoders);
	for (i = 0; i < resptr->count_encoders; i++)
		printf("encoder[%d] = %d\n", i, resptr->encoders[i]);

	printf("min_width: %d max_width: %d\n",
		resptr->min_width, resptr->max_width);
	printf("min_height: %d max_height: %d\n",
		resptr->min_height, resptr->max_height);
	printf("\n");

				/* first connected connector */

	conn = NULL;
	for (i = 0; i < resptr->count_connectors; i++) {
		conn = drmModeGetConnector(drm, resptr->connectors[1]);
		if (conn->connection == DRM_MODE_CONNECTED)
			break;
		else
			conn = NULL;
	}
	if (conn == NULL) {
		printf("no available connector\n");
		exit(1);
	}

				/* find crtc and select mode within */

	printf("enc: %d\n", conn->encoder_id);
	if (conn->encoder_id)
		enc = drmModeGetEncoder(drm, conn->encoder_id);
	else {
		printf("no encoder\n");
		return NULL;			// to be completed
	}
	if (enc->crtc_id)
		crtc = enc->crtc_id;
	else {
		printf("no crtc\n");
		return NULL;			// to be completed
	}
	drmModeFreeEncoder(enc);
	printf("crtc: %d\n", crtc);
	mode = &conn->modes[nmode];		// but not necessarily
	printf("res: %dx%d\n", mode->hdisplay, mode->vdisplay);
	printf("\n");

				/* create dumb framebuffer */

	memset(&createdumb, 0, sizeof(createdumb));
	createdumb.width = mode->hdisplay;
	createdumb.height = mode->vdisplay;
	createdumb.bpp = 32;			// TODO: ???
	createdumb.flags = 0;
	printf("DRM_IOCTL_MODE_CREATE_DUMB width=%d height=%d bpp=%d\n",
		createdumb.width, createdumb.height, createdumb.bpp);
	res = drmIoctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &createdumb);
	printf("DRM_IOCTL_MODE_CREATE_DUMB: %s\n", strerror(-res));
	printf("size: %llu handle: %d\n", createdumb.size, createdumb.handle);

	printf("drmModeAddFB width=%d height=%d 24 32 %d handle=%d\n",
		createdumb.width, createdumb.height,
		createdumb.pitch, createdumb.handle);
	res = drmModeAddFB(drm,
		createdumb.width, createdumb.height,
		24, 32, // createdumb.bpp,
		createdumb.pitch,
		createdumb.handle, &buf_id);
	printf("drmModeAddFB: %d buf_id=%d\n", res, buf_id);

	memset(&mapdumb, 0, sizeof(mapdumb));
	mapdumb.handle = createdumb.handle;
	mapdumb.pad = 0;
	printf("drmIoctl DRM_IOCTL_MODE_MAP_DUMB %d\n", mapdumb.handle);
	res = drmIoctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &mapdumb);
	printf("DRM_IOCTL_MODE_MAP_DUMB: %s\n", strerror(-res));
	printf("offset: %llu\n", mapdumb.offset);

				/* connect framebuffer to crtc */

	res = drmModeSetCrtc(drm, crtc, buf_id, 0, 0,
			     &conn->connector_id, 1,
			     &conn->modes[nmode]);
	printf("drmModeSetCrtc: %d\n", res);

				/* map surface to memory */

	printf("mmap: size=%llu drm=%d offset=%llu\n",
		createdumb.size, drm, mapdumb.offset);
	img = mmap(NULL, createdumb.size,
		PROT_READ | PROT_WRITE, MAP_SHARED, drm, mapdumb.offset);
	if (img == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	dbuf = doublebuffering ? malloc(createdumb.size) : img;

				/* create cairo */

	format = CAIRO_FORMAT_RGB24; // or CAIRO_FORMAT_RGB16_565;
	width = createdumb.width;
	height = createdumb.height;
	stride = createdumb.pitch;
	surface = cairo_image_surface_create_for_data(dbuf, format,
		width, height, stride);
	status = cairo_surface_status(surface);
	if (status != CAIRO_STATUS_SUCCESS)
		printf("WARNING: cairo status=%d\n", status);
	cr = cairo_create(surface);

				/* fill structure */

	cairodrm = malloc(sizeof(struct cairodrm));
	cairodrm->surface = surface;
	cairodrm->cr = cr;
	cairodrm->width = width;
	cairodrm->height = height;
	cairodrm->dev = drm;
	cairodrm->handle = createdumb.handle;
	cairodrm->buf_id = buf_id;
	cairodrm->img = img;
	cairodrm->dbuf = dbuf;
	cairodrm->size = createdumb.size;
	return cairodrm;
}

/*
 * clear the cairo context
 */
void cairodrm_clear(struct cairodrm *cairodrm,
		double red, double green, double blue) {
	cairo_identity_matrix(cairodrm->cr);
	cairo_set_source_rgb(cairodrm->cr, red, green, blue);
	cairo_rectangle(cairodrm->cr, 0, 0, cairodrm->width, cairodrm->height);
	cairo_fill(cairodrm->cr);
}

/*
 * return whether double buffering is used
 */
int cairodrm_doublebuffering(struct cairodrm *cairodrm) {
	return cairodrm->img != cairodrm->dbuf;
}

/*
 * flush the cairo context
 */
void cairodrm_flush(struct cairodrm *cairodrm) {
	drmModeClip clip;
	int res;

	if (cairodrm_doublebuffering(cairodrm))
		memcpy(cairodrm->img, cairodrm->dbuf, cairodrm->size);
	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = cairodrm->width;
	clip.y2 = cairodrm->height;
	res = drmModeDirtyFB(cairodrm->dev, cairodrm->buf_id, &clip, 1);
	printf("drmModeDirtyFB: %d\n", res);
}

/*
 * deallocate and close
 */
void cairodrm_finish(struct cairodrm *cairodrm) {
	struct drm_mode_destroy_dumb destroydumb;
	int res;

	cairo_destroy(cairodrm->cr);
	cairo_surface_destroy(cairodrm->surface);
	munmap(cairodrm->img, cairodrm->size);

	drmModeRmFB(cairodrm->dev, cairodrm->buf_id);

	destroydumb.handle = cairodrm->handle;
	res = drmIoctl(cairodrm->dev,
		DRM_IOCTL_MODE_DESTROY_DUMB, &destroydumb);
	printf("DRM_IOCTL_MODE_DESTROY_DUMB: %s\n", strerror(-res));
	close(cairodrm->dev);
}


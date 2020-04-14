/*
 * cairodrm.c
 *
 * a cairo context for drawing on a linux framebuffer
 *
 * to do:
 * - allow to activate only some of the connector(s)
 * - pass a resolution instead of always targeting the best;
 *   may be a maximal width and height to use for the cairo context
 */

/*
 * Internals
 * ---------
 *
 * The framebuffers are the memory areas used for drawing. The connectors are
 * the actual video outputs of the device (VGA, HDMI, AV, LVDS, etc.). In order
 * to produce video, a framebuffer needs to be created and linked to the
 * connectors.
 *
 * The case of a single connector is easy: choose a mode of the connector,
 * create a framebuffer as large as that mode and link it to the connector.
 *
 * The complication with multiple connectors is that they may have different
 * modes, with different size. They may also differ in aspect, like in the
 * following (exaggerated) example.
 *
 *
 *         +----------------+ <---- mode of connector 1
 *         |                |
 *    +----|----------------|----+ <--- mode of connector 2
 *    |    |                |    |
 *    |    |                |    |
 *    |    |                |    |
 *    +----|----------------|----+
 *         |                |
 *         +----------------+
 *
 * A mode of a connector can be seen as the size of a possible viewport on a
 * framebuffer, like a camera that takes only a part of a larger image. Linking
 * a framebuffer to a connector requires:
 *
 * - the mode to use
 *   this tells the resolution of the video output,
 *   but also the size of the viewport on the framebuffer
 * - the position of the viewport within the framebuffer
 *
 * This allows to link the same framebuffer to connectors set to different
 * modes. The viewports cannot be larger than the framebuffer. Seen in the
 * other way, the framebuffer must be large enough to contain all these modes.
 * Its weight must be at least the maximal among the weights of the modes and
 * its height the maximal of the heights. Such a framebuffer can be linked to
 * all connectors by placing each connector viewport at its center.
 *
 * In order for the same image to be shown on all connector, it must be drawn
 * on the common part of these viewports. This is the intersection of the two
 * rectangles in the example above. This intersection is what is used for
 * creating a cairo context. This way, the cairo context is visualized at the
 * center of each connected video device, possiblly leaving black bands on the
 * sides or on at the top and bottom.
 *
 * To use the maximal possible resolution while minimizing the size of the
 * black bands, so that the image is as large as possible on all video outputs,
 * the size of the framebuffer and the modes are calculated as follows.
 *
 * 1. for each connector, find its maximal resolution mode
 * 2. the minimal width and height of these modes are the size of the cairo
 *    context
 * 3. for each connector, its mode is the one of minimal size among the ones
 *    large enough to contain the whole cairo context
 * 4. the size of the framebuffer is the maximal width and maximal height among
 *    all these modes
 *
 * Since the size of the modes comprises two numbers (width and height), the
 * maximal (in step 1) and minimal (in step 3) are not always unique. The code
 * here keeps the current best (maximal or minimal so far) and updates it only
 * when a new mode is better on both measures. The minimization in step 2 and
 * the maximization in step 4 are instead done separately on the widths and on
 * the heights, and are therefore unique.
 *
 * Actually, the modes of a connector are not necessarily the only ones
 * supported by the connector. The code here is however designed to only employ
 * them.
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
 * parse a connector string
 */
int *enabledconnectors(drmModeResPtr resptr, char *connectors) {
	int *enabled;
	char *scan, c;
	unsigned int conn;
	int i, res;

	printf("enabled connectors\n");
	enabled = malloc(resptr->count_connectors * sizeof(int));
	for (i = 0; i < resptr->count_connectors; i++) {
		if (connectors == NULL || ! strcmp(connectors, "all"))
			enabled[i] = 1;
		else {
			enabled[i] = 0;
			for (scan = connectors; scan != NULL; ) {
				res = sscanf(scan, "%u%c", &conn, &c);
				if (res == 1 || (res == 2 && c == ','))
					if (conn == resptr->connectors[i])
						enabled[i] = 1;
				scan = index(scan, ',');
				if (scan)
					scan++;
			}
		}
		printf("\tconnector %d: %s\n", resptr->connectors[i],
			enabled[i] ? "enabled" : "disabled");
	}

	return enabled;
}

/*
 * maximal-resolution mode of a connector
 */
int _maximalmode(drmModeConnectorPtr conn, drmModeResPtr resptr) {
	int i;
	unsigned int w, h;
	int max;

	w = resptr->max_width + 1;
	h = resptr->max_height + 1;
	max = 0;
	for (i = 0; i < conn->count_modes; i++) {
		printf("\t\t\tmode %2d: %d x %d\n", i,
			conn->modes[i].hdisplay,
			conn->modes[i].vdisplay);
		if (w < conn->modes[i].hdisplay &&
		    h < conn->modes[i].vdisplay) {
			w = conn->modes[i].hdisplay;
			h = conn->modes[i].vdisplay;
			max = i;
		}
	}
	printf("\t\tmode %d: %d x %d\n", max,
		conn->modes[max].hdisplay, conn->modes[max].vdisplay);
	return max;
}

/*
 * maximal resolution supported by all connectors
 */
int _maximalcommon(int drm, drmModeResPtr resptr, int *enabled,
		unsigned *width, unsigned *height) {
	drmModeConnectorPtr conn;
	int i, j;

	*width = resptr->max_width + 1;
	*height = resptr->max_height + 1;
	printf("determine maximal common resolution\n");
	for (i = 0; i < resptr->count_connectors; i++) {
		conn = drmModeGetConnector(drm, resptr->connectors[i]);
		printf("\tconnector %d\n", conn->connector_id);
		if (! enabled[i] || conn->connection != DRM_MODE_CONNECTED) {
			puts(! enabled[i] ? "\t\tdisabled": "\t\tunconnected");
			drmModeFreeConnector(conn);
			continue;
		}
		j = _maximalmode(conn, resptr);
		if (*width > conn->modes[j].hdisplay)
			*width = conn->modes[j].hdisplay;
		if (*height > conn->modes[j].vdisplay)
			*height = conn->modes[j].vdisplay;
		drmModeFreeConnector(conn);
	}
	if (*width == resptr->max_width + 1 ||
	    *height == resptr->max_height + 1) {
		printf("\tno available modes\n");
		return -1;
	}
	printf("\tmaximal common size: %dx%d\n", *width, *height);
	return 0;
}

/*
 * minimal-resolution mode of a connector of the given size or more
 */
int _minimalmode(drmModeConnectorPtr conn, drmModeResPtr resptr,
		int width, int height) {
	int i;
	unsigned int w, h;
	int min;

	w = resptr->max_width + 1;
	h = resptr->max_height + 1;
	min = 0;
	for (i = 0; i < conn->count_modes; i++) {
		printf("\t\t\tmode %2d: %d x %d\n", i,
			conn->modes[i].hdisplay,
			conn->modes[i].vdisplay);
		if (conn->modes[i].hdisplay < width)
			continue;
		if (conn->modes[i].vdisplay < height)
			continue;
		if (w > conn->modes[i].hdisplay &&
		    h > conn->modes[i].vdisplay) {
			w = conn->modes[i].hdisplay;
			h = conn->modes[i].vdisplay;
			min = i;
		}
	}
	printf("\t\tmode %d: %d x %d\n", min,
		conn->modes[min].hdisplay, conn->modes[min].vdisplay);
	return min;
}

/*
 * minimal framebuffer size
 */
int _framebuffersize(int drm, drmModeResPtr resptr, int *enabled,
		int reqwidth, int reqheight,
		unsigned *width, unsigned *height) {
	drmModeConnectorPtr conn;
	int i, j;

	*width = 0;
	*height = 0;
	printf("determine framebuffer size\n");
	for (i = 0; i < resptr->count_connectors; i++) {
		conn = drmModeGetConnector(drm, resptr->connectors[i]);
		printf("\tconnector %d\n", conn->connector_id);
		if (! enabled[i] || conn->connection != DRM_MODE_CONNECTED) {
			puts(! enabled[i] ? "\t\tdisabled": "\t\tunconnected");
			drmModeFreeConnector(conn);
			continue;
		}
		j = _minimalmode(conn, resptr, reqwidth, reqheight);
		if (*width < conn->modes[j].hdisplay)
			*width = conn->modes[j].hdisplay;
		if (*height < conn->modes[j].vdisplay)
			*height = conn->modes[j].vdisplay;
		drmModeFreeConnector(conn);
	}
	if (*width == 0 || *height == 0) {
		printf("\tno available modes\n");
		return -1;
	}
	printf("\tframebuffer size: %dx%d\n", *width, *height);
	return 0;
}

/*
 * create, add and map a framebuffer
 */
uint32_t _createframebuffer(int drm, int width, int height, int bpp,
		uint64_t *size, uint64_t *offset, uint32_t *stride,
		uint32_t *handle) {
	struct drm_mode_create_dumb createdumb;
	struct drm_mode_map_dumb mapdumb;
	uint32_t buf_id;
	int res;

	printf("create framebuffer\n");
	memset(&createdumb, 0, sizeof(createdumb));
	createdumb.width = width;
	createdumb.height = height;
	createdumb.bpp = bpp;
	createdumb.flags = 0;
	printf("\tcreate width=%d height=%d bpp=%d\n",
		createdumb.width, createdumb.height, createdumb.bpp);
	res = drmIoctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &createdumb);
	printf("\t\tresult: %s\n", strerror(-res));
	printf("\t\tsize: %llu\n", createdumb.size);
	printf("\t\thandle: %d\n", createdumb.handle);

	printf("\tadd width=%d height=%d 24 32 pitch=%d handle=%d\n",
		createdumb.width, createdumb.height,
		createdumb.pitch, createdumb.handle);
	res = drmModeAddFB(drm,
		createdumb.width, createdumb.height,
		24, 32, // createdumb.bpp,
		createdumb.pitch,
		createdumb.handle, &buf_id);
	printf("\t\tresult: %s\n", strerror(-res));
	printf("\t\tbuf_id: %d\n", buf_id);

	memset(&mapdumb, 0, sizeof(mapdumb));
	mapdumb.handle = createdumb.handle;
	mapdumb.pad = 0;
	printf("\tmap handle=%d\n", mapdumb.handle);
	res = drmIoctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &mapdumb);
	printf("\t\tresult: %s\n", strerror(-res));
	printf("\t\toffset: %llu\n", mapdumb.offset);

	*size = createdumb.size;
	*offset = mapdumb.offset;
	*stride = createdumb.pitch;
	*handle = createdumb.handle;
	return buf_id;
}

/*
 * link a framebuffer to the connectors
 */
int _linkframebufferconnectors(int drm, drmModeResPtr resptr, int *enabled,
		int buf_id,
		int width, int height,
		int fbwidth, int fbheight,
		int *cwidth, int *cheight) {
	int i;
	drmModeConnectorPtr conn;
	drmModeEncoderPtr enc;
	int nmode;
	int res;
	unsigned x, y;

	printf("link framebuffer to connector(s)\n");
	*cwidth = resptr->max_width + 1;
	*cheight = resptr->max_height + 1;
	for (i = 0; i < resptr->count_connectors; i++) {
		conn = drmModeGetConnector(drm, resptr->connectors[i]);
		printf("\tconnector %d\n", conn->connector_id);
		if (! enabled[i] || conn->connection != DRM_MODE_CONNECTED) {
			puts(! enabled[i] ? "\t\tdisabled": "\t\tunconnected");
			drmModeFreeConnector(conn);
			continue;
		}
		if (conn->encoder_id == 0) {
			printf("no encoder\n");
			exit(1);		// search an encoder
		}
		enc = drmModeGetEncoder(drm, conn->encoder_id);
		if (enc->crtc_id == 0) {
			printf("no crtc\n");
			exit(1);		// search a crtc
		}
		nmode = _minimalmode(conn, resptr, width, height);
		x = (fbwidth - conn->modes[nmode].hdisplay) / 2;
		y = (fbheight - conn->modes[nmode].vdisplay) / 2;
		printf("\t\tdisplacement: x=%d y=%d\n", x, y);
		res = drmModeSetCrtc(drm, enc->crtc_id, buf_id, x, y,
			&conn->connector_id, 1, &conn->modes[nmode]);
		printf("\t\tresult: %s\n", strerror(-res));
		if (*cwidth > conn->modes[nmode].hdisplay)
			*cwidth = conn->modes[nmode].hdisplay;
		if (*cheight > conn->modes[nmode].vdisplay)
			*cheight = conn->modes[nmode].vdisplay;
		drmModeFreeEncoder(enc);
		drmModeFreeConnector(conn);
	}
	printf("\tintersection: %d x %d\n", *cwidth, *cheight);

	return 0;
}

/*
 * create a cairo context from a drm device
 */
struct cairodrm *cairodrm_init(char *devname, int doublebuffering,
		char *connectors) {
	unsigned width, height, bpp = 32;

	int drm, res;
	uint64_t supportdumb;
	drmModeResPtr resptr;
	int *enabled;

	uint64_t size, offset;
	uint32_t pitch, handle;

	uint32_t buf_id;

	unsigned char *img, *dbuf, *pos;
	unsigned int fbwidth, fbheight;
	int stride;

	int cwidth, cheight;
	unsigned x, y;
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

				/* get resources */

	resptr = drmModeGetResources(drm);

				/* enabled connectors */

	enabled = enabledconnectors(resptr, connectors);

				/* maximal shared resolution */

	res = _maximalcommon(drm, resptr, enabled, &width, &height);
	if (res)
		return NULL;
	// width = 820;
	// height = 200;

				/* size of framebuffer */

	res = _framebuffersize(drm, resptr, enabled, width, height, &fbwidth, &fbheight);
	if (res)
		return NULL;

				/* create dumb framebuffer */

	buf_id = _createframebuffer(drm, fbwidth, fbheight, bpp,
	                            &size, &offset, &pitch, &handle);

				/* link framebuffer -> connectors */

	res = _linkframebufferconnectors(drm, resptr, enabled, buf_id,
		width, height, fbwidth, fbheight, &cwidth, &cheight);

				/* map surface to memory */

	printf("mmap size=%llu drm=%d offset=%llu\n", size, drm, offset);
	img = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm, offset);
	if (img == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	dbuf = doublebuffering ? malloc(size) : img;

				/* create the cairo context */

	format = CAIRO_FORMAT_RGB24; // or CAIRO_FORMAT_RGB16_565;
	stride = pitch;
	x = (fbwidth - cwidth) / 2;
	y =  (fbheight - cheight) / 2;
	pos = dbuf + bpp / 8 * x + stride * y;
	surface = cairo_image_surface_create_for_data(pos, format,
		cwidth, cheight, stride);
	status = cairo_surface_status(surface);
	if (status != CAIRO_STATUS_SUCCESS)
		printf("WARNING: cairo status=%d\n", status);
	cr = cairo_create(surface);

				/* fill structure */

	cairodrm = malloc(sizeof(struct cairodrm));
	cairodrm->surface = surface;
	cairodrm->cr = cr;
	cairodrm->width = cwidth;
	cairodrm->height = cheight;
	cairodrm->dev = drm;
	cairodrm->handle = handle;
	cairodrm->buf_id = buf_id;
	cairodrm->img = img;
	cairodrm->dbuf = dbuf;
	cairodrm->size = size;
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
	cairo_stroke(cairodrm->cr);
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
	printf("drmModeDirtyFB: %s\n", strerror(-res));
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
	if (cairodrm_doublebuffering(cairodrm))
		free(cairodrm->dbuf);

	res = drmModeRmFB(cairodrm->dev, cairodrm->buf_id);
	printf("remove framebuffer: %s\n", strerror(-res));

	destroydumb.handle = cairodrm->handle;
	res = drmIoctl(cairodrm->dev,
		DRM_IOCTL_MODE_DESTROY_DUMB, &destroydumb);
	printf("destroy framebuffer handle=%d: %s\n",
	       destroydumb.handle, strerror(-res));
	close(cairodrm->dev);
}


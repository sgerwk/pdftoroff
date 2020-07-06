/*
 * cairodrm.c
 *
 * a cairo context for drawing on the linux direct rendering infrastructure
 */

/*
 * Internals
 * ---------
 *
 * The framebuffers are the memory areas where the programs draw. The
 * connectors are the actual video outputs of the device (VGA, HDMI, AV, LVDS,
 * etc.). Each connector supports certain resolutions (modes), like 1280x1024,
 * 1024x768, 800x600, etc. In order to produce video, a framebuffer needs to be
 * created and linked to the connectors, each set on its mode (resolution).
 *
 * The case of a single connector is easy: choose a mode of the connector,
 * create a framebuffer as large as that mode and link it to the connector.
 *
 * The complication with multiple connectors is that they may support different
 * modes (resolutions). These may even differ in aspect, like in the following
 * (exaggerated) example.
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
 * framebuffer, like a camera that takes only a part of a larger image. The
 * video output from that connector shows only the part of the framebuffer that
 * is inside the viewport.
 *
 * Linking a framebuffer to a connector requires:
 *
 * - the mode to use
 *   this tells the resolution of the video output,
 *   but also the size of the viewport of the connector on the framebuffer
 * - the position of the viewport within the framebuffer
 *
 * This allows to link the same framebuffer to connectors set to different
 * modes. The viewports cannot be larger than the framebuffer. Seen in the
 * other way, the framebuffer must be large enough to contain all these modes.
 * Its weight must be at least the maximal among the weights of the modes and
 * its height the maximal of the heights. Such a framebuffer can be linked to
 * all connectors by placing each connector viewport at its center.
 *
 * In order for the same image to be shown in full on all connector, it must be
 * drawn on the common part of these viewports. This is the intersection of the
 * two rectangles in the example above. This intersection is what is used for
 * creating a cairo context. This way, the cairo context is visualized at the
 * center of each connected video device, possiblly leaving black bands on the
 * sides or on at the top and bottom.
 *
 * To use the maximal possible resolution while minimizing the size of the
 * black bands so that the image is as large as possible on all video outputs,
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
 * when another mode is better on both dimensions. The minimization in step 2
 * and the maximization in step 4 are instead done separately on the widths and
 * the heights, and are therefore unique.
 *
 * Actually, the modes of a connector are not necessarily the only ones
 * supported by the connector. The code here is however designed to only employ
 * them.
 */

/*
 * Parameters
 * ----------
 *
 * This module takes a list of connectors to use and a requested size.
 *
 * When a list of connectors is passed, only the connectors in it are used. The
 * others are excluded from the calculation of the size of the framebuffer and
 * the cairo context and are not linked to the framebuffer. They do not show
 * any output and do not affect the resolution and size of the output on the
 * others.
 *
 * When a requested size is passed, steps 1. and 2. of the calculation of the
 * size of the framebuffer and cairo context are skipped. Only 3. and 4.
 * remain: the size of the framebuffer is calculated to include a mode large
 * enough to contain an area of the requested size on all connectors, and the
 * cairo context is the intersection of them. When a flag "exact" is also
 * passed, the cairo context size is instead exactly the requested size. This
 * allows:
 *
 * - without "exact", a resolution lower than maximal
 * - with "exact", to optmize the output toward a specific connector at the
 *   expense of the others
 *
 * The second is also obtained by passing "id" or "type" as the requested size.
 * The best resolution for the connector of that id or type is used as the
 * requested size. The pdf file is shown at full screen at the maximal
 * resolution on that connectors; the others may show it with a black frame or
 * only the central part of it.
 */

/*
 * Virtual terminal switching
 * --------------------------
 *
 * When the virtual terminal switches out, the original framebuffer-connector
 * links have to be restored. They are saved in the cairodrm->prev array before
 * changing them.
 *
 * The cairodrm->curr array contains the framebuffer-connector links as changed
 * by the initialization. They are restored when switching the virtual terminal
 * in.
 *
 * - init:
 *   . cairodrm->prev = framebuffer-connector links
 *   . change framebuffer-connector links
 *   . cairodrm->curr = framebuffer-connector links
 * - switch terminal out:
 *     framebuffer-connector links = cairodrm->prev
 * - switch terminal in:
 *     framebuffer-connector links = cairodrm->curr
 *
 * Also drmDropMaster(), drmSetMaster() are called when switching the terminal
 * out and in, but they are only necessary when switching between drm
 * applications on the same virtual terminal, and fail unless called by root.
 */

#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <cairo.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "cairodrm.h"

/*
 * connector types
 */
struct {
	char *name;	unsigned value;
} connectorarray[] = {
	{"unknown",	DRM_MODE_CONNECTOR_Unknown},
	{"vga",		DRM_MODE_CONNECTOR_VGA},
	{"dvii",	DRM_MODE_CONNECTOR_DVII},
	{"dvi",		DRM_MODE_CONNECTOR_DVII},
	{"dvid",	DRM_MODE_CONNECTOR_DVID},
	{"dvi",		DRM_MODE_CONNECTOR_DVID},
	{"dvia",	DRM_MODE_CONNECTOR_DVIA},
	{"dvi",		DRM_MODE_CONNECTOR_DVIA},
	{"composite",	DRM_MODE_CONNECTOR_Composite},
	{"svideo",	DRM_MODE_CONNECTOR_SVIDEO},
	{"lvds",	DRM_MODE_CONNECTOR_LVDS},
	{"component",	DRM_MODE_CONNECTOR_Component},
	{"9pindin",	DRM_MODE_CONNECTOR_9PinDIN},
	{"displayport",	DRM_MODE_CONNECTOR_DisplayPort},
	{"hdmia",	DRM_MODE_CONNECTOR_HDMIA},
	{"hdmi",	DRM_MODE_CONNECTOR_HDMIA},
	{"hdmib",	DRM_MODE_CONNECTOR_HDMIB},
	{"hdmi",	DRM_MODE_CONNECTOR_HDMIB},
	{"tv",		DRM_MODE_CONNECTOR_TV},
	{"edp",		DRM_MODE_CONNECTOR_eDP},
	{"virtual",	DRM_MODE_CONNECTOR_VIRTUAL},
	{"dsi",		DRM_MODE_CONNECTOR_DSI},
	{NULL,		0}
};

/*
 * list connector types
 */
void listconnectors(int drm, drmModeResPtr resptr, int *enabled, int modes) {
	int i, j;
	drmModeConnectorPtr conn;

	for (i = 0; i < resptr->count_connectors; i++) {
		if (! enabled[i])
			continue;
		conn = drmModeGetConnector(drm, resptr->connectors[i]);
		printf("connector %d: ", conn->connector_id);
		for (j = 0; connectorarray[j].name; j++)
			if (connectorarray[j].value == conn->connector_type) {
				printf("%s", connectorarray[j].name);
				break;
			}
		if (modes)
			for (j = 0; j < conn->count_modes; j++)
				printf(" %dx%d",
					conn->modes[j].hdisplay,
					conn->modes[j].vdisplay);
		printf("\n");
		drmModeFreeConnector(conn);
	}
}

/*
 * match a connector with a specification
 */
int matchconnector(drmModeConnectorPtr conn, char *spec) {
	int i;
	if ((unsigned) atoi(spec) == conn->connector_id)
		return 1;

	for (i = 0; connectorarray[i].name; i++)
		if (! strcmp(spec, connectorarray[i].name) &&
		    connectorarray[i].value == conn->connector_type)
			return 1;
	return 0;
}

/*
 * parse a connector string
 */
int *enabledconnectors(int drm, drmModeResPtr resptr, char *connectors) {
	int *enabled;
	char *scan, field[100], c;
	drmModeConnectorPtr conn;
	int i, res;

	printf("enabled connectors\n");
	enabled = malloc(resptr->count_connectors * sizeof(int));
	for (i = 0; i < resptr->count_connectors; i++) {
		if (connectors == NULL ||
		    strstr(connectors, "all") ||
		    ! strcmp(connectors, "list"))
			enabled[i] = 1;
		else {
			conn = drmModeGetConnector(drm, resptr->connectors[i]);
			enabled[i] = 0;
			for (scan = connectors; scan != NULL; ) {
				res = sscanf(scan, "%90[^,]%c", field, &c);
				if (res == 1 || (res == 2 && c == ','))
					if (matchconnector(conn, field))
						enabled[i] = 1;
				scan = index(scan, ',');
				if (scan)
					scan++;
			}
			drmModeFreeConnector(conn);
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
int _linkframebufferconnectors(int drm,
		drmModeResPtr resptr, int *enabled,
		drmModeCrtcPtr *prev, drmModeCrtcPtr *curr,
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
		prev[i] = NULL;
		curr[i] = NULL;

		printf("\tconnector %d\n", resptr->connectors[i]);
		if (! enabled[i]) {
			puts("\t\tdisabled");
			continue;
		}
		conn = drmModeGetConnector(drm, resptr->connectors[i]);
		if (conn->connection != DRM_MODE_CONNECTED) {
			puts("\t\tunconnected");
			drmModeFreeConnector(conn);
			continue;
		}

		if (conn->encoder_id == 0) {
			printf("no encoder\n");
			exit(1);
			// search for an encoder
			// also: save previous and current,
			// restore in _restoreframebufferconnectors()
		}
		enc = drmModeGetEncoder(drm, conn->encoder_id);

		if (enc->crtc_id == 0) {
			printf("no crtc\n");
			exit(1);
			// search for a crtc
			// also: save previous and current,
			// restore in _restoreframebufferconnectors()
		}

		prev[i] = drmModeGetCrtc(drm, enc->crtc_id);

		nmode = _minimalmode(conn, resptr, width, height);
		x = (fbwidth - conn->modes[nmode].hdisplay) / 2;
		y = (fbheight - conn->modes[nmode].vdisplay) / 2;
		printf("\t\tdisplacement: x=%d y=%d\n", x, y);
		res = drmModeSetCrtc(drm, enc->crtc_id, buf_id, x, y,
			&conn->connector_id, 1, &conn->modes[nmode]);
		printf("\t\tresult: %s\n", strerror(-res));

		curr[i] = drmModeGetCrtc(drm, enc->crtc_id);

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
 * restore saved framebuffers-connectors links
 */
void _restoreframebufferconnectors(int drm, drmModeResPtr resptr,
		drmModeCrtcPtr *saved) {
	int i;
	int res;

	printf("restoring framebuffer-connector links\n");
	for (i = 0; i < resptr->count_connectors; i++) {
		printf("\tconnector %d\n", resptr->connectors[i]);
		if (saved[i] == NULL) {
			printf("\t\tnot saved\n");
			continue;
		}
		// restore previous encoder and its crtc if changed
		res = drmModeSetCrtc(drm, saved[i]->crtc_id,
			saved[i]->buffer_id, saved[i]->x, saved[i]->y,
			&resptr->connectors[i], 1, &saved[i]->mode);
		printf("\t\tresult: %s\n", strerror(-res));
	}
}

/*
 * create a cairo context from a drm device
 */
struct cairodrm *cairodrm_init(char *devname,
		char *connectors, char *size, int flags) {
	unsigned width, height, bpp = 32;

	int drm, res;
	uint64_t supportdumb;
	drmModeResPtr resptr;
	int *enabled, *sizeenabled;

	uint64_t fbsize, offset;
	uint32_t pitch, handle;

	uint32_t buf_id;
	drmModeCrtcPtr *prev, *curr;

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

	enabled = enabledconnectors(drm, resptr, connectors);

				/* list connectors */

	if ((connectors != NULL && strstr(connectors, "list")) ||
	    (size != NULL && ! strcmp(size, "list"))) {
		listconnectors(drm, resptr, enabled,
			size != NULL && ! strcmp(size, "list"));
		drmModeFreeResources(resptr);
		return NULL;
	}

				/* maximal shared resolution */

	if (size == NULL || 2 != sscanf(size, "%dx%d", &width, &height)) {
		if (size == NULL)
			sizeenabled = enabled;
		else {
			sizeenabled = enabledconnectors(drm, resptr, size);
			flags |= CAIRODRM_EXACT;
		}
		res = _maximalcommon(drm, resptr, sizeenabled, &width, &height);
		if (sizeenabled != enabled)
			free(sizeenabled);
		if (res) {
			drmModeFreeResources(resptr);
			return NULL;
		}
	}
				/* size of framebuffer */

	res = _framebuffersize(drm, resptr, enabled,
		width, height, &fbwidth, &fbheight);
	if (res) {
		drmModeFreeResources(resptr);
		return NULL;
	}

				/* create dumb framebuffer */

	buf_id = _createframebuffer(drm, fbwidth, fbheight, bpp,
	                            &fbsize, &offset, &pitch, &handle);

				/* link framebuffer -> connectors */

	prev = malloc(resptr->count_connectors * sizeof(drmModeCrtcPtr));
	curr = malloc(resptr->count_connectors * sizeof(drmModeCrtcPtr));
	res = _linkframebufferconnectors(drm, resptr, enabled, prev, curr,
		buf_id, width, height, fbwidth, fbheight, &cwidth, &cheight);
	if (flags & CAIRODRM_EXACT) {
		cwidth = width;
		cheight = height;
	}

				/* map framebuffer to memory */

	printf("mmap size=%" PRIu64 "drm=%d offset=%" PRIu64 "\n",
		fbsize, drm, offset);
	img = mmap(NULL, fbsize,
	           PROT_READ | PROT_WRITE, MAP_SHARED, drm, offset);
	if (img == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	dbuf = (flags & CAIRODRM_DOUBLEBUFFERING) ? malloc(fbsize) : img;

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
	cairodrm->size = fbsize;
	cairodrm->resptr = resptr;
	cairodrm->enabled = enabled;
	cairodrm->prev = prev;
	cairodrm->curr = curr;
	return cairodrm;
}

/*
 * switch in and out a virtual terminal
 */
void cairodrm_switcher(struct cairodrm *cairodrm, int inout) {
	int res;

	if (inout == 0) {
		printf(">>> switch vt out\n");
		_restoreframebufferconnectors(cairodrm->dev,
			cairodrm->resptr, cairodrm->prev);
		res = drmDropMaster(cairodrm->dev);	// ok if fails
		printf("drmDropMaster: %s\n", strerror(-res));
	}
	else {
		printf(">>> switch vt in\n");
		res = drmSetMaster(cairodrm->dev);	// ok if fails
		printf("drmSetMaster: %s\n", strerror(-res));
		_restoreframebufferconnectors(cairodrm->dev,
			cairodrm->resptr, cairodrm->curr);
	}
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
	int i;

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

	for (i = 0; i < cairodrm->resptr->count_connectors; i++) {
		if (cairodrm->prev[i] != NULL)
			drmModeFreeCrtc(cairodrm->prev[i]);
		if (cairodrm->curr[i] != NULL)
			drmModeFreeCrtc(cairodrm->curr[i]);
	}
	free(cairodrm->prev);
	free(cairodrm->curr);
	drmModeFreeResources(cairodrm->resptr);
	free(cairodrm->enabled);

	close(cairodrm->dev);
	free(cairodrm);
}


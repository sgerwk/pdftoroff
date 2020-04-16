/*
 * cairodrm-main.c
 *
 * testing program for cairodrm.{c,h}
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <cairo.h>
#include "cairodrm.h"

int main(int argc, char *argv[]) {
	struct cairodrm *cairodrm;

				/* create a cairodrm */

	(void) argc;

	cairodrm = cairodrm_init("/dev/dri/card0", argv[1], 0, 0, 0);
	if (cairodrm == NULL)
		exit(EXIT_FAILURE);

				/* draw something */

	cairodrm_clear(cairodrm, 1.0, 1.0, 1.0);
	cairo_set_source_rgb(cairodrm->cr, 0.0, 1.0, 0.0);
	cairo_rectangle(cairodrm->cr,
		cairodrm->width - 100, cairodrm->height - 100, 100, 100);
	cairo_fill(cairodrm->cr);

	cairo_select_font_face(cairodrm->cr, "serif",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cairodrm->cr, 32.0);
	cairo_set_source_rgb(cairodrm->cr, 0.0, 0.0, 1.0);
	cairo_move_to(cairodrm->cr, 10.0, 50.0);
	cairo_show_text(cairodrm->cr, "Hello, world");

	cairo_set_source_rgb(cairodrm->cr, 1.0, 0.0, 0.0);
	cairo_set_line_width(cairodrm->cr, 5.0);
	cairo_move_to(cairodrm->cr, 20.0, 10.0);
	cairo_line_to(cairodrm->cr, 220.0, 70.0);
	cairo_stroke(cairodrm->cr);

				/* flush */

	cairodrm_flush(cairodrm);
	getchar();

				/* finish */

	cairodrm_finish(cairodrm);

	return EXIT_SUCCESS;
}

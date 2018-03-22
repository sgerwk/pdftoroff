/*
 * vt.h
 *
 * vt switch handling
 */

#ifdef _VT_H
#else
#define _VT_H

/* terminal is suspended (switched out) */
extern int vt_suspend;

/* terminal needs redrawing */
extern int vt_redraw;

/* setup virtual terminal for suspend and resume */
void vt_setup();

#endif


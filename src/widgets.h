/* widgets.h -- meters, graphs and dialogs drawn on a canvas.
   Copyright (C) 2026 Richard L.

   This file is part of Dials.

   Dials is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Dials is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Dials.  If not, see <https://www.gnu.org/licenses/>.  */

#ifndef DIALS_WIDGETS_H
#define DIALS_WIDGETS_H

#include "canvas.h"
#include "metric.h"

/* Return ST_OK, ST_WARN or ST_CRIT for a load fraction.  */
int level_style (double frac);

/* Format V in at most four characters, such as "980" or "31k".  */
void fmt_compact (char *buf, size_t size, double v);

/* Draw "LABEL ████░░░░  54%  TEXT" on row Y.  LABEL_W and TEXT_W
   reserve columns so that several meters line up.  FRAC is NaN when
   unknown; TEXT may be NULL.  */
void widget_meter (struct canvas *cv, int y, int x, int w,
                   const char *label, int label_w, double frac,
                   const char *text, int text_w);

/* Draw the history in M as a bar graph inside R, newest sample on the
   right.  SCALE is the top of the graph; colors follow the fraction
   of CBASE.  */
void widget_graph (struct canvas *cv, struct rect r, const struct metric *m,
                   double scale, double cbase);

/* The same inside a single-line frame with TITLE, labeled with the
   time span covered when sampling every INTERVAL seconds.  */
void widget_graph_box (struct canvas *cv, struct rect r, const char *title,
                       const struct metric *m, double scale, double cbase,
                       double interval);

/* Draw a centered dialog box with N LINES of text.  */
void widget_dialog (struct canvas *cv, const char *title,
                    const char *const *lines, int n, bool center);

#endif /* DIALS_WIDGETS_H */

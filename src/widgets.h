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
int level_style (double frac) ATTRIBUTE_CONST;

/* Format V in at most four characters, such as "980" or "31k".  */
void fmt_compact (char *buf, size_t size, double v);

/* Format a size in MiB in at most four characters: "512K", "512M",
   "6.0G".  */
void fmt_axis_mib (char *buf, size_t size, double mib);

/* Draw "LABEL ████░░░░  54%  TEXT" on row Y.  LABEL_W and TEXT_W
   reserve columns so that several meters line up.  FRAC is NaN when
   unknown; TEXT may be NULL.  */
void widget_meter (struct canvas *cv, int y, int x, int w,
                   const char *label, int label_w, double frac,
                   const char *text, int text_w);

/* Draw the history in M as a bar graph inside R, newest sample on the
   right.  SCALE is the top of the graph; colors follow the fraction
   of CBASE.  AXIS formats the axis labels; NULL means fmt_compact.  */
void widget_graph (struct canvas *cv, struct rect r, const struct metric *m,
                   double scale, double cbase,
                   void (*axis) (char *, size_t, double));

/* The same inside a single-line frame with TITLE, labeled with the
   time span covered when sampling every INTERVAL seconds.  */
void widget_graph_box (struct canvas *cv, struct rect r, const char *title,
                       const struct metric *m, double scale, double cbase,
                       void (*axis) (char *, size_t, double),
                       double interval);

/* Draw a framed block: TITLE on the frame, VALUE in style ST, a bar
   for FRAC, and the lowest, mean and highest samples of M formatted
   by FMT.  R should be 5 rows high.  */
void widget_stat_block (struct canvas *cv, struct rect r, const char *title,
                        const char *value, double frac, int st,
                        const struct metric *m,
                        void (*fmt) (char *, size_t, double));

/* Draw "LABEL VALUE" at X if it fits before END.  Return where the
   next pair goes, or X if this one did not fit.  */
int widget_pair (struct canvas *cv, int y, int x, int end, const char *label,
                 const char *value);

/* The same, after a mark in style ST, for the legend of a stacked bar.  */
int widget_legend (struct canvas *cv, int y, int x, int end, int st,
                   const char *label, const char *value);

/* Draw a bar W wide made of N parts: FRACS[i] of it in STYLES[i], the
   rest empty.  */
void widget_stacked_bar (struct canvas *cv, int y, int x, int w,
                         const double *fracs, const int *styles, int n);

/* Draw N rows of LABELS and VALUES from Y, X, the values lined up after
   the widest label, in at most W columns.  */
void widget_values (struct canvas *cv, int y, int x, int w,
                    const char *const *labels, const char *const *values,
                    int n);

/* A table column.  */
struct table_col
{
  const char *title;		/* Already translated.  */
  int width;			/* 0: share the remaining space.  */
  bool right;			/* Right-align the cells.  */
  int drop;			/* 0: always shown; when space is short,
                                   higher numbers are hidden first.  */
};

/* Store in WIDTHS the width of each of the N columns of a table W
   columns wide; hidden columns get 0.  */
void table_layout (const struct table_col *cols, int n, int w, int *widths);

/* Draw one row of N CELLS at Y, X, W wide, in style ROW_ST.  STYLES,
   if not NULL, gives a style per cell, or -1 for ROW_ST.  */
void table_row (struct canvas *cv, int y, int x, int w,
                const struct table_col *cols, int n, const int *widths,
                const char *const *cells, const int *styles, int row_st);

/* Draw a centered dialog box with N LINES of text.  */
void widget_dialog (struct canvas *cv, const char *title,
                    const char *const *lines, int n, bool center);

#endif /* DIALS_WIDGETS_H */

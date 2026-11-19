/* page_sensors.c -- the Sensors page.
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

#include "system.h"

#include <stdio.h>

#include "app.h"
#include "pages.h"
#include "sensors.h"
#include "theme.h"
#include "tui.h"
#include "util.h"
#include "widgets.h"

enum { GRAPH_H = 8 };

/* Rows of the table shown at the last drawing, for paging.  */
static int shown_rows = 10;

static void
draw_graph (struct app *a, struct canvas *cv, struct rect r,
            const struct sensor *sn)
{
  double lo, avg, hi;
  double scale = sn->limit;
  if (isnan (scale))
    scale = metric_stats (&sn->m, &lo, &avg, &hi) && hi > 0 ? hi * 1.2 : 1;
  char v[32], t[128];
  fmt_sensor (v, sizeof v, sn->kind, sn->value);
  snprintf (t, sizeof t, "%s %s  %s", sn->chip, sn->label, v);
  widget_graph_box (cv, r, t, &sn->m, scale,
                    isnan (sn->limit) ? 0 : sn->limit, NULL, a->interval);
}

void
page_sensors_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct sensors *s = &a->sensors;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Sensors"), ST_TITLE);
  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  if (s->n == 0)
    {
      canvas_put_center (cv, y + 1, x, w,
                         _("No hardware monitoring sensors found."), ST_LABEL);
      return;
    }
  if (a->sensor_sel >= (size_t) s->n)
    a->sensor_sel = s->n - 1;

  const struct table_col cols[] = {
    { _("Chip"), 16, false, 0 },
    { _("Sensor"), 0, false, 0 },
    { _("Now"), 10, true, 0 },
    { _("Lowest"), 10, true, 1 },
    { _("Highest"), 10, true, 1 },
    { _("Limit"), 10, true, 2 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  int graph_h = bottom - y >= GRAPH_H + 6 ? GRAPH_H : 0;
  int rows = bottom - y - graph_h - 1;
  shown_rows = rows > 1 ? rows : 1;

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);

  int first = (int) a->sensor_sel >= rows ? (int) a->sensor_sel - rows + 1 : 0;
  for (int row = 0; row < rows && first + row < s->n; row++)
    {
      int i = first + row;
      const struct sensor *sn = &s->s[i];
      bool sel = (size_t) i == a->sensor_sel;
      char now[32], lo[32], hi[32], lim[32];
      fmt_sensor (now, sizeof now, sn->kind, sn->value);
      fmt_sensor (lo, sizeof lo, sn->kind, sn->lo);
      fmt_sensor (hi, sizeof hi, sn->kind, sn->hi);
      if (isnan (sn->limit))
        lim[0] = '\0';
      else
        fmt_sensor (lim, sizeof lim, sn->kind, sn->limit);
      /* Name each chip once, on its first row.  */
      bool first_of_chip = row == 0 || strcmp (sn->chip, s->s[i - 1].chip) != 0;
      const char *cells[N] = { first_of_chip ? sn->chip : "", sn->label, now,
                               lo, hi, lim };
      int styles[N] = { ST_TITLE, -1, ST_VALUE, -1, -1, ST_DIM };
      if (!isnan (sn->limit) && !isnan (sn->value) && sn->limit > 0)
        styles[2] = level_style (sn->value / sn->limit);
      if (sel)
        for (int k = 0; k < N; k++)
          styles[k] = -1;
      table_row (cv, y + 1 + row, x, w, cols, N, widths, cells, styles,
                 sel ? ST_SEL : ST_BASE);
    }

  if (graph_h)
    draw_graph (a, cv, (struct rect) { bottom - graph_h, r.x + 1, graph_h,
                                       r.w - 2 }, &s->s[a->sensor_sel]);
}

bool
page_sensors_key (struct app *a, int ch)
{
  size_t n = (size_t) a->sensors.n, page = (size_t) shown_rows;
  if (n == 0)
    return false;
  switch (ch)
    {
    case KEY_UP:
      if (a->sensor_sel > 0)
        a->sensor_sel--;
      return true;
    case KEY_DOWN:
      if (a->sensor_sel + 1 < n)
        a->sensor_sel++;
      return true;
    case KEY_PPAGE:
      a->sensor_sel = a->sensor_sel > page ? a->sensor_sel - page : 0;
      return true;
    case KEY_NPAGE:
      a->sensor_sel = a->sensor_sel + page < n ? a->sensor_sel + page : n - 1;
      return true;
    case KEY_HOME:
      a->sensor_sel = 0;
      return true;
    case KEY_END:
      a->sensor_sel = n - 1;
      return true;
    default:
      return false;
    }
}

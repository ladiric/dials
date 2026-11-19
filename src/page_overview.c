/* page_overview.c -- the Overview page.
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
#include "theme.h"
#include "util.h"
#include "widgets.h"

static int
max_width (const char *const *s, int n)
{
  int w = 0;
  for (int i = 0; i < n; i++)
    {
      int sw = text_width (s[i]);
      if (sw > w)
        w = sw;
    }
  return w;
}

static void
title_with_pct (char *buf, size_t size, const char *label, double pct)
{
  if (isnan (pct))
    snprintf (buf, size, "%s", label);
  else
    snprintf (buf, size, "%s  %.0f%%", label, pct);
}

static double
fraction (double part, double whole)
{
  return whole > 0 ? part / whole : NAN;
}

void
page_overview_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct sys_info *in = &a->info;
  const struct sys_sample *s = &a->cur;
  char kernel[200], up[64], load[64], ncpu[16], sampling[64];

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _(pages[a->page].title), ST_TITLE);

  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  /* System information in two columns.  */
  snprintf (kernel, sizeof kernel, "%s %s", in->kernel, in->arch);
  fmt_uptime (up, sizeof up, s->uptime);
  if (isnan (s->load[0]))
    snprintf (load, sizeof load, "%s", _("n/a"));
  else
    snprintf (load, sizeof load, "%.2f %.2f %.2f",
              s->load[0], s->load[1], s->load[2]);
  snprintf (ncpu, sizeof ncpu, "%d", in->ncpu);
  fmt_interval (sampling, sizeof sampling, a->interval);

  const char *l1[4] = { _("Host"), _("System"), _("Kernel"), _("Processor") };
  const char *v1[4] = { in->host, in->os, kernel, in->cpu_model };
  const char *l2[4] = { _("Uptime"), _("Load"), _("CPUs"), _("Sampling") };
  const char *v2[4] = { up, load, ncpu, sampling };
  int lw1 = max_width (l1, 4), lw2 = max_width (l2, 4);
  int vw2 = max_width (v2, 4);
  int x2 = x + w - (lw2 + 2 + vw2);
  bool two = x2 - (x + lw1 + 2) >= 20;

  for (int i = 0; i < 4; i++)
    {
      int vx = x + lw1 + 2;
      canvas_put (cv, y + i, x, lw1, l1[i], ST_LABEL);
      canvas_put (cv, y + i, vx, (two ? x2 - 2 : x + w) - vx, v1[i],
                  ST_VALUE);
      if (two)
        {
          canvas_put (cv, y + i, x2, lw2, l2[i], ST_LABEL);
          canvas_put (cv, y + i, x2 + lw2 + 2, vw2, v2[i], ST_VALUE);
        }
    }
  y += 4;

  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, _("Usage"), ST_TITLE);
  y++;

  /* Meters.  */
  char mem[64], swap[64];
  fmt_used_total (mem, sizeof mem, s->mem_used, s->mem_total);
  if (s->swap_total > 0)
    fmt_used_total (swap, sizeof swap, s->swap_used, s->swap_total);
  else
    snprintf (swap, sizeof swap, "%s", _("no swap"));
  const char *ml[3] = { _("CPU"), _("Memory"), _("Swap") };
  const char *mt[2] = { mem, swap };
  int mlw = max_width (ml, 3), mtw = max_width (mt, 2);
  widget_meter (cv, y, x, w, ml[0], mlw, s->cpu_pct / 100, NULL, mtw);
  widget_meter (cv, y + 1, x, w, ml[1], mlw,
                fraction (s->mem_used, s->mem_total), mem, mtw);
  widget_meter (cv, y + 2, x, w, ml[2], mlw,
                fraction (s->swap_used, s->swap_total), swap, mtw);
  y += 3;

  /* History graphs side by side.  */
  if (bottom - y >= 5)
    {
      struct rect g1 = { y, r.x + 1, bottom - y, (r.w - 2) / 2 };
      struct rect g2 = { y, g1.x + g1.w, bottom - y, r.w - 2 - g1.w };
      char t1[64], t2[64];
      title_with_pct (t1, sizeof t1, _("CPU"), metric_last (&a->m_cpu));
      title_with_pct (t2, sizeof t2, _("Memory"), metric_last (&a->m_mem));
      widget_graph_box (cv, g1, t1, &a->m_cpu, 100, 100, a->interval);
      widget_graph_box (cv, g2, t2, &a->m_mem, 100, 100, a->interval);
    }
}

/* page_cpu.c -- the CPU page.
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

#include <config.h>

#include "system.h"

#include <stdio.h>

#include "app.h"
#include "cpu.h"
#include "pages.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

enum { GRAPH_MIN = 5 };

static void
fmt_short_mhz (char *buf, size_t size, double mhz)
{
  if (isnan (mhz))
    snprintf (buf, size, "%s", "");
  else if (mhz >= 1000)
    snprintf (buf, size, "%.1fG", mhz / 1000);
  else
    snprintf (buf, size, "%.0fM", mhz);
}

static void
draw_details (struct app *a, struct canvas *cv, int y, int x, int end)
{
  const struct cpu *c = &a->cpu;
  const struct sys_sample *s = &a->cur;
  char b[4][64];
  int cx;

  snprintf (b[0], sizeof b[0], "%d", c->n);
  cx = widget_pair (cv, y, x, end, _("Processor"), a->info.cpu_model);
  widget_pair (cv, y, cx, end, _("CPUs"), b[0]);

  fmt_mhz (b[0], sizeof b[0], c->mhz_avg);
  fmt_mhz (b[1], sizeof b[1], c->mhz_top);
  fmt_mhz (b[2], sizeof b[2], c->mhz_max);
  fmt_temp (b[3], sizeof b[3], c->temp);
  cx = widget_pair (cv, y + 1, x, end, _("Frequency"), b[0]);
  cx = widget_pair (cv, y + 1, cx, end, _("Highest"), b[1]);
  if (!isnan (c->mhz_max))
    cx = widget_pair (cv, y + 1, cx, end, _("Maximum"), b[2]);
  if (!isnan (c->temp))
    widget_pair (cv, y + 1, cx, end, _("Temperature"), b[3]);

  if (isnan (s->load[0]))
    snprintf (b[0], sizeof b[0], "%s", _("n/a"));
  else
    snprintf (b[0], sizeof b[0], "%.2f %.2f %.2f",
              s->load[0], s->load[1], s->load[2]);
  fmt_pct (b[1], sizeof b[1], c->psi);
  snprintf (b[2], sizeof b[2], "%.0f", c->running);
  snprintf (b[3], sizeof b[3], "%.0f", c->blocked);
  cx = widget_pair (cv, y + 2, x, end, _("Load"), b[0]);
  if (!isnan (c->psi))
    cx = widget_pair (cv, y + 2, cx, end, _("Waiting"), b[1]);
  if (!isnan (c->running))
    cx = widget_pair (cv, y + 2, cx, end, _("Running"), b[2]);
  if (!isnan (c->blocked))
    widget_pair (cv, y + 2, cx, end, _("Blocked"), b[3]);

  fmt_count_rate (b[0], sizeof b[0], c->ctxt);
  fmt_count_rate (b[1], sizeof b[1], c->intr);
  fmt_count_rate (b[2], sizeof b[2], c->forks);
  cx = widget_pair (cv, y + 3, x, end, _("Context switches"), b[0]);
  cx = widget_pair (cv, y + 3, cx, end, _("Interrupts"), b[1]);
  widget_pair (cv, y + 3, cx, end, _("New processes"), b[2]);
}

/* The total use split by kind, and its legend below.  */
static void
draw_split (const struct cpu *c, struct canvas *cv, int y, int x, int w)
{
  const struct cpu_split *sp = &c->split;
  const char *labels[] = {
    _("user"), _("nice"), _("system"), _("I/O wait"), _("IRQ"), _("steal")
  };
  double pct[] = { sp->user, sp->nice, sp->system, sp->iowait, sp->irq,
                   sp->steal };
  static const int styles[] = { ST_OK, ST_FRAME, ST_CRIT, ST_WARN,
                                ST_ACCENT, ST_VALUE };
  enum { N = ARRAY_CARDINALITY (styles) };
  double fracs[N];
  char total[16];

  for (int i = 0; i < N; i++)
    fracs[i] = pct[i] / 100;
  const char *label = _("Total");
  int lw = text_width (label) + 1;
  canvas_put (cv, y, x, lw, label, ST_LABEL);
  fmt_pct (total, sizeof total, c->total);
  int bw = w - lw - 6;
  if (bw > 0)
    widget_stacked_bar (cv, y, x + lw, bw, fracs, styles, N);
  canvas_put_right (cv, y, x + w - 5, 5, total, ST_VALUE);

  int cx = x;
  for (int i = 0; i < N; i++)
    {
      char v[16];
      fmt_pct (v, sizeof v, pct[i]);
      cx = widget_legend (cv, y + 1, cx, x + w, styles[i], labels[i], v);
    }
}

static int
digits (int n)
{
  int d = 1;
  while (n >= 10)
    {
      n /= 10;
      d++;
    }
  return d;
}

/* One cell per processor, in columns, filling at most H rows.  Cells
   show a bar and the frequency when there is room, and less when
   there is not.  Return the rows used.  */
static int
draw_cores (const struct cpu *c, struct canvas *cv, int y, int x, int w,
            int h)
{
  enum { GAP = 2, PCT_W = 5, MHZ_W = 6, BAR_MIN = 3, CELL_MAX = 44 };
  int n = c->n, idw = digits (n - 1);
  if (h < 1 || w < idw + PCT_W)
    return 0;

  int cols = (n + h - 1) / h;
  if (cols < w / CELL_MAX)
    cols = w / CELL_MAX;
  if (cols > n)
    cols = n;
  if (cols < 1)
    cols = 1;
  int cell = w / cols;
  if (cell < idw + PCT_W + 1)
    {
      cols = w / (idw + PCT_W + 1);
      cell = w / cols;
    }
  int rows = (n + cols - 1) / cols;
  if (rows > h)
    rows = h;

  bool show_mhz = cell >= idw + 1 + BAR_MIN + PCT_W + MHZ_W + GAP;
  int bw = cell - idw - 1 - PCT_W - (show_mhz ? MHZ_W : 0) - GAP;
  bool show_bar = bw >= BAR_MIN;

  for (int i = 0; i < n; i++)
    {
      int col = i / rows, row = i % rows;
      if (col >= cols)
        break;
      int cx = x + col * cell, cy = y + row;
      double pct = c->core[i].pct;
      char id[16], p[16], mhz[16];

      snprintf (id, sizeof id, "%d", i);
      canvas_put_right (cv, cy, cx, idw, id, ST_LABEL);
      cx += idw + 1;
      if (show_bar)
        {
          int filled = isnan (pct) ? 0 : (int) (pct / 100 * bw + 0.5);
          canvas_repeat (cv, cy, cx, filled, L'█', level_style (pct / 100));
          canvas_repeat (cv, cy, cx + filled, bw - filled, L'░', ST_DIM);
          cx += bw;
        }
      if (isnan (pct))
        snprintf (p, sizeof p, "%s", _("off"));
      else
        snprintf (p, sizeof p, "%.0f%%", pct);
      canvas_put_right (cv, cy, cx, PCT_W, p,
                        isnan (pct) ? ST_DIM : level_style (pct / 100));
      cx += PCT_W;
      if (show_mhz)
        {
          fmt_short_mhz (mhz, sizeof mhz, c->core[i].mhz);
          canvas_put_right (cv, cy, cx, MHZ_W, mhz, ST_DIM);
        }
    }
  return rows;
}

static void
draw_graphs (struct app *a, struct canvas *cv, struct rect r)
{
  const struct cpu *c = &a->cpu;
  char t1[96], t2[96], v[32];
  struct rect g1 = { r.y, r.x, r.h, r.w / 2 };
  struct rect g2 = { r.y, r.x + g1.w, r.h, r.w - g1.w };

  fmt_pct (v, sizeof v, metric_last (&c->m_total));
  snprintf (t1, sizeof t1, "%s  %s", _("Usage"), v);
  widget_graph_box (cv, g1, t1, &c->m_total, 100, 100, NULL, a->interval);

  /* Temperature where the hardware reports it, frequency elsewhere.  */
  if (!isnan (metric_last (&c->m_temp)))
    {
      fmt_temp (v, sizeof v, metric_last (&c->m_temp));
      snprintf (t2, sizeof t2, "%s  %s", _("Temperature"), v);
      widget_graph_box (cv, g2, t2, &c->m_temp, 100, 100, NULL, a->interval);
    }
  else
    {
      double lo, avg, hi, scale = c->mhz_max;
      if (isnan (scale))
        scale = metric_stats (&c->m_mhz, &lo, &avg, &hi) ? hi : 1000;
      fmt_mhz (v, sizeof v, metric_last (&c->m_mhz));
      snprintf (t2, sizeof t2, "%s  %s", _("Frequency"), v);
      widget_graph_box (cv, g2, t2, &c->m_mhz, scale, scale, NULL,
                        a->interval);
    }
}

void
page_cpu_draw (struct app *a, struct canvas *cv, struct rect r)
{
  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("CPU"), ST_TITLE);

  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  draw_details (a, cv, y, x, x + w);
  y += 4;
  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, _("Usage"), ST_TITLE);
  draw_split (&a->cpu, cv, y + 1, x, w);
  y += 3;
  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, _("Processors"), ST_TITLE);
  y++;

  /* The grid takes what it needs; the graphs get the rest if that is
     enough to be useful.  */
  int avail = bottom - y;
  int cols_min = w / 44 > 0 ? w / 44 : 1;
  int need = (a->cpu.n + cols_min - 1) / cols_min;
  int grid_h = need < avail - GRAPH_MIN ? need : avail - GRAPH_MIN;
  if (grid_h < 1)
    grid_h = avail;
  y += draw_cores (&a->cpu, cv, y, x, w, grid_h);
  if (bottom - y >= GRAPH_MIN)
    draw_graphs (a, cv, (struct rect) { y, r.x + 1, bottom - y, r.w - 2 });
}

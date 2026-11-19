/* page_gpu.c -- the GPU page.
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
#include "gpu.h"
#include "pages.h"
#include "theme.h"
#include "tui.h"
#include "util.h"
#include "widgets.h"

/* From top to bottom: the list of GPUs, a line of details, four stat
   blocks, four graphs and the processes.  When space is short the
   stat blocks go first, then the processes; the graphs stay.  */
enum
{
  LIST_ROWS = 4,		/* GPUs shown before the list scrolls.  */
  STAT_H = 5,
  GRAPH_ROW_MIN = 5,		/* A graph frame with three rows of bars.  */
  PROC_MIN = 4			/* Divider, header and two rows.  */
};

static double
ratio (double part, double whole)
{
  return whole > 0 ? part / whole : NAN;
}

/* Store a short status for S in BUF and return its style.  */
static int
gpu_status (const struct gpu_sample *s, char *buf, size_t size)
{
  if (s->temp >= GPU_TEMP_ALERT)
    {
      char t[32];
      fmt_temp (t, sizeof t, s->temp);
      snprintf (buf, size, _("HOT %s"), t);
      return ST_ALERT;
    }
  double used = 100 * ratio (s->mem_used, s->mem_total);
  if (used >= GPU_MEM_ALERT)
    {
      snprintf (buf, size, _("VRAM %.0f%%"), used);
      return ST_ALERT;
    }
  snprintf (buf, size, "%s", _("OK"));
  return ST_OK;
}

static void
draw_list (struct app *a, struct canvas *cv, int y, int x, int w, int rows)
{
  const struct gpus *g = &a->gpus;
  const struct table_col cols[] = {
    { "", 1, false, 0 },
    { "#", 2, true, 0 },
    { _("Name"), 0, false, 0 },
    { _("Temp"), 6, true, 0 },
    { _("Util"), 5, true, 0 },
    { _("Memory"), 18, true, 1 },
    { _("Power"), 11, true, 2 },
    { _("Fan"), 4, true, 3 },
    { _("Status"), 10, false, 0 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);

  int first = (int) a->gpu_sel >= rows ? (int) a->gpu_sel - rows + 1 : 0;
  for (int row = 0; row < rows && first + row < g->count; row++)
    {
      int i = first + row;
      const struct gpu_sample *s = &g->dev[i].cur;
      bool sel = (size_t) i == a->gpu_sel;
      char num[16], temp[32], util[16], mem[48], power[48], fan[16];
      char status[48];

      snprintf (num, sizeof num, "%d", i);
      fmt_temp (temp, sizeof temp, s->temp);
      fmt_pct (util, sizeof util, s->util);
      fmt_used_total (mem, sizeof mem, s->mem_used, s->mem_total);
      fmt_of_limit (power, sizeof power, s->power, s->power_limit, "W");
      fmt_pct (fan, sizeof fan, s->fan);
      int status_st = gpu_status (s, status, sizeof status);

      const char *cells[N] = {
        sel ? "►" : "", num, g->dev[i].name, temp, util, mem, power, fan,
        status
      };
      int styles[N] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
      if (status_st == ST_ALERT || !sel)
        styles[N - 1] = status_st;
      table_row (cv, y + 1 + row, x, w, cols, N, widths, cells, styles,
                 sel ? ST_SEL : ST_BASE);
    }
}

static void
fmt_clock (char *buf, size_t size, double cur, double max)
{
  fmt_of_limit (buf, size, cur, max, "MHz");
}

static void
draw_details (struct app *a, struct canvas *cv, int y, int x, int w,
              const struct gpu_sample *s)
{
  int end = x + w;
  char buf[64];

  if (a->gpus.driver[0])
    x = widget_pair (cv, y, x, end, _("Driver"), a->gpus.driver);
  if (s->pstate >= 0)
    {
      snprintf (buf, sizeof buf, "P%d", s->pstate);
      x = widget_pair (cv, y, x, end, _("State"), buf);
    }
  if (!isnan (s->clock_sm))
    {
      fmt_clock (buf, sizeof buf, s->clock_sm, s->clock_sm_max);
      x = widget_pair (cv, y, x, end, _("Clock"), buf);
    }
  if (!isnan (s->clock_mem))
    {
      fmt_clock (buf, sizeof buf, s->clock_mem, s->clock_mem_max);
      x = widget_pair (cv, y, x, end, _("Memory clock"), buf);
    }
  if (!isnan (s->fan))
    {
      fmt_pct (buf, sizeof buf, s->fan);
      x = widget_pair (cv, y, x, end, _("Fan"), buf);
    }
  if (!isnan (s->enc))
    {
      fmt_pct (buf, sizeof buf, s->enc);
      x = widget_pair (cv, y, x, end, _("Encoder"), buf);
    }
  if (!isnan (s->dec))
    {
      fmt_pct (buf, sizeof buf, s->dec);
      widget_pair (cv, y, x, end, _("Decoder"), buf);
    }
}

/* The four things measured over time: what they are called, how they
   are shown, and the values that scale their bars and colors.  */
struct gpu_metric
{
  const char *title;
  const struct metric *m;
  void (*fmt) (char *, size_t, double);
  double scale;			/* Top of the bar and the graph.  */
  double cbase;			/* Colors are fractions of this.  */
  void (*axis) (char *, size_t, double);	/* Graph labels, or NULL.  */
};

static void
gpu_metrics (const struct gpu_dev *d, struct gpu_metric out[4])
{
  const struct gpu_sample *s = &d->cur;
  double lo, avg, hi;

  /* Without a power limit, scale power to the highest reading.  */
  double plimit = s->power_limit;
  if (isnan (plimit))
    plimit = metric_stats (&d->power, &lo, &avg, &hi) && hi > 0 ? hi * 1.1 : 100;

  out[0] = (struct gpu_metric) { _("Temperature"), &d->temp, fmt_temp,
                                 100, GPU_TEMP_ALERT, NULL };
  out[1] = (struct gpu_metric) { _("Utilization"), &d->util, fmt_pct,
                                 100, 100, NULL };
  out[2] = (struct gpu_metric) { _("Memory"), &d->mem, fmt_mib,
                                 s->mem_total, s->mem_total, fmt_axis_mib };
  out[3] = (struct gpu_metric) { _("Power"), &d->power, fmt_watts,
                                 plimit, plimit, NULL };
}

static void
draw_stats (struct canvas *cv, int y, int x, int w, const struct gpu_dev *d)
{
  const struct gpu_sample *s = &d->cur;
  struct gpu_metric gm[4];
  char v[4][64];

  gpu_metrics (d, gm);
  fmt_temp (v[0], sizeof v[0], s->temp);
  fmt_pct (v[1], sizeof v[1], s->util);
  fmt_used_total (v[2], sizeof v[2], s->mem_used, s->mem_total);
  fmt_of_limit (v[3], sizeof v[3], s->power, s->power_limit, "W");

  int bw = w / 4;
  for (int i = 0; i < 4; i++)
    {
      double cur = metric_last (gm[i].m);
      double level = ratio (cur, gm[i].cbase);
      struct rect r = { y, x + i * bw, STAT_H, i < 3 ? bw : w - 3 * bw };
      widget_stat_block (cv, r, gm[i].title, v[i], ratio (cur, gm[i].scale),
                         isnan (level) ? ST_VALUE : level_style (level),
                         gm[i].m, gm[i].fmt);
    }
}

static void
draw_graphs (struct app *a, struct canvas *cv, struct rect r,
             const struct gpu_dev *d)
{
  struct gpu_metric gm[4];
  gpu_metrics (d, gm);

  int rows = r.h >= 2 * GRAPH_ROW_MIN ? 2 : 1;
  int cols = rows == 2 || r.w < 64 ? 2 : 4;
  int n = rows * cols < 4 ? rows * cols : 4;
  for (int i = 0; i < n; i++)
    {
      int row = i / cols, col = i % cols;
      int h0 = r.h / rows, w0 = r.w / cols;
      struct rect g = {
        r.y + row * h0, r.x + col * w0,
        row == rows - 1 ? r.h - row * h0 : h0,
        col == cols - 1 ? r.w - col * w0 : w0
      };
      char value[64], title[128];
      gm[i].fmt (value, sizeof value, metric_last (gm[i].m));
      snprintf (title, sizeof title, "%s  %s", gm[i].title, value);
      widget_graph_box (cv, g, title, gm[i].m, gm[i].scale, gm[i].cbase,
                        gm[i].axis, a->interval);
    }
}

static void
draw_procs (struct app *a, struct canvas *cv, int y, int x, int w, int h,
            const struct gpu_sample *s)
{
  const struct table_col cols[] = {
    { _("PID"), 8, true, 0 },
    { _("Type"), 8, false, 2 },
    { _("GPU"), 5, true, 1 },
    { _("Memory"), 9, true, 0 },
    { _("Command"), 0, false, 0 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);

  if (!a->gpus.procs_valid)
    {
      canvas_put_center (cv, y + 1, x, w, _("Reading processes..."), ST_DIM);
      return;
    }
  if (s->nproc == 0)
    {
      canvas_put_center (cv, y + 1, x, w,
                         _("No process is using this GPU."), ST_DIM);
      return;
    }
  for (int i = 0; i < s->nproc && i < h - 1; i++)
    {
      const struct gpu_proc *p = &s->proc[i];
      char pid[16], sm[16], mem[32];
      snprintf (pid, sizeof pid, "%u", p->pid);
      if (isnan (p->sm))
        snprintf (sm, sizeof sm, "%s", "-");
      else
        fmt_pct (sm, sizeof sm, p->sm);
      if (isnan (p->mem))
        snprintf (mem, sizeof mem, "%s", "-");
      else
        fmt_mib (mem, sizeof mem, p->mem);
      const char *type = (p->compute && p->graphics ? _("both")
                          : p->compute ? _("compute") : _("graphics"));
      const char *cells[N] = { pid, type, sm, mem, p->name };
      table_row (cv, y + 1 + i, x, w, cols, N, widths, cells, NULL, ST_BASE);
    }
}

static void
draw_no_gpu (struct app *a, struct canvas *cv, struct rect r)
{
  const char *lines[] = {
    _("There is no GPU to show."),
    a->gpus.error,
    "",
    _("Run 'dials --demo' to see this page with simulated GPUs."),
  };
  int n = ARRAY_CARDINALITY (lines);
  int y = r.y + (r.h - n) / 2;
  for (int i = 0; i < n; i++)
    canvas_put_center (cv, y + i, r.x + 2, r.w - 4, lines[i],
                       i == 0 ? ST_TITLE : ST_LABEL);
}

void
page_gpu_draw (struct app *a, struct canvas *cv, struct rect r)
{
  struct gpus *g = &a->gpus;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("GPU"), ST_TITLE);
  if (g->count == 0)
    {
      draw_no_gpu (a, cv, r);
      return;
    }
  if (a->gpu_sel >= (size_t) g->count)
    a->gpu_sel = g->count - 1;
  const struct gpu_dev *d = &g->dev[a->gpu_sel];

  int x = r.x + 1, w = r.w - 2, y = r.y + 1;
  int bottom = r.y + r.h - 1;	/* The frame's last row.  */

  int rows = g->count < LIST_ROWS ? g->count : LIST_ROWS;
  draw_list (a, cv, y, x + 1, w - 2, rows);
  y += 1 + rows;

  char head[160];
  snprintf (head, sizeof head, _("GPU %zu: %s"), a->gpu_sel, d->name);
  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, head, ST_TITLE);
  y++;
  draw_details (a, cv, y, x + 1, w - 2, &d->cur);
  y++;

  int rest = bottom - y;
  if (rest >= STAT_H + 2 * GRAPH_ROW_MIN + PROC_MIN)
    {
      draw_stats (cv, y, x, w, d);
      y += STAT_H;
      rest -= STAT_H;
    }

  int proc_h = 0;
  if (rest >= GRAPH_ROW_MIN + PROC_MIN)
    {
      int want = 2 + (d->cur.nproc > 0 ? d->cur.nproc : 1);
      proc_h = rest * 2 / 5;
      if (proc_h > want)
        proc_h = want;
      if (proc_h < PROC_MIN)
        proc_h = PROC_MIN;
    }
  int graph_h = rest - proc_h;
  if (graph_h >= 3)
    draw_graphs (a, cv, (struct rect) { y, x, graph_h, w }, d);
  y += graph_h;

  if (proc_h)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("Processes"), ST_TITLE);
      draw_procs (a, cv, y + 1, x + 1, w - 2, proc_h - 1, &d->cur);
    }
}

bool
page_gpu_key (struct app *a, int ch)
{
  size_t n = (size_t) a->gpus.count;
  if (n == 0)
    return false;
  switch (ch)
    {
    case KEY_UP:
      if (a->gpu_sel > 0)
        a->gpu_sel--;
      return true;
    case KEY_DOWN:
      if (a->gpu_sel + 1 < n)
        a->gpu_sel++;
      return true;
    case KEY_HOME:
      a->gpu_sel = 0;
      return true;
    case KEY_END:
      a->gpu_sel = n - 1;
      return true;
    default:
      return false;
    }
}

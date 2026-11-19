/* page_disk.c -- the Disks page.
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
#include "disk.h"
#include "pages.h"
#include "theme.h"
#include "tui.h"
#include "util.h"
#include "widgets.h"

enum
{
  LIST_ROWS = 6,		/* Disks shown before the list scrolls.  */
  GRAPH_MIN = 5,
  FS_MIN = 3,			/* Divider, header and a row.  */
  USE_BAR = 8			/* Width of the bar in the Use column.  */
};

static void
fmt_ms (char *buf, size_t size, double ms)
{
  if (isnan (ms))
    snprintf (buf, size, "%s", _("n/a"));
  else if (ms < 10)
    snprintf (buf, size, "%.1f ms", ms);
  else
    snprintf (buf, size, "%.0f ms", ms);
}

static void
draw_list (struct app *a, struct canvas *cv, int y, int x, int w, int rows)
{
  const struct disks *d = &a->disks;
  const struct table_col cols[] = {
    { "", 1, false, 0 },
    { _("Device"), 8, false, 0 },
    { _("Model"), 0, false, 2 },
    { _("Size"), 7, true, 3 },
    { _("Read"), 10, true, 0 },
    { _("Write"), 10, true, 0 },
    { _("Busy"), 5, true, 0 },
    { _("Wait"), 8, true, 1 },
    { _("Temp"), 6, true, 1 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);

  int first = (int) a->disk_sel >= rows ? (int) a->disk_sel - rows + 1 : 0;
  for (int row = 0; row < rows && first + row < d->n; row++)
    {
      int i = first + row;
      const struct disk_dev *dev = &d->dev[i];
      bool sel = (size_t) i == a->disk_sel;
      char size[32], rd[32], wr[32], busy[16], wait[16], temp[16];
      fmt_bytes (size, sizeof size, dev->size);
      fmt_rate (rd, sizeof rd, dev->rd_bps);
      fmt_rate (wr, sizeof wr, dev->wr_bps);
      fmt_pct (busy, sizeof busy, dev->util);
      fmt_ms (wait, sizeof wait, dev->await);
      if (isnan (dev->temp))
        snprintf (temp, sizeof temp, "%s", "");
      else
        fmt_temp (temp, sizeof temp, dev->temp);
      const char *cells[N] = {
        sel ? "►" : "", dev->name, dev->model, size, rd, wr, busy, wait, temp
      };
      int styles[N] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
      if (!sel && !isnan (dev->util))
        styles[6] = level_style (dev->util / 100);
      table_row (cv, y + 1 + row, x, w, cols, N, widths, cells, styles,
                 sel ? ST_SEL : ST_BASE);
    }
}

static void
draw_details (const struct disk_dev *dev, struct canvas *cv, int y, int x,
              int end)
{
  char b[64];
  int cx = widget_pair (cv, y, x, end, _("Type"),
                        dev->rotational ? _("hard disk") : _("solid state"));
  fmt_bytes (b, sizeof b, dev->size);
  cx = widget_pair (cv, y, cx, end, _("Size"), b);
  fmt_count_rate (b, sizeof b, dev->rd_iops);
  cx = widget_pair (cv, y, cx, end, _("Reads"), b);
  fmt_count_rate (b, sizeof b, dev->wr_iops);
  cx = widget_pair (cv, y, cx, end, _("Writes"), b);
  snprintf (b, sizeof b, "%.0f", dev->inflight);
  if (!isnan (dev->inflight))
    cx = widget_pair (cv, y, cx, end, _("In progress"), b);
  fmt_ms (b, sizeof b, dev->await);
  widget_pair (cv, y, cx, end, _("Wait"), b);
}

/* Scale a throughput graph to the most seen, so that a quiet disk
   still shows its small transfers.  */
static double
rate_scale (const struct metric *m)
{
  double lo, avg, hi;
  if (!metric_stats (m, &lo, &avg, &hi) || hi < 1)
    return 1;
  return hi * 1.1;
}

static void
draw_graphs (struct app *a, struct canvas *cv, struct rect r,
             const struct disk_dev *dev)
{
  const struct metric *ms[3] = { &dev->m_rd, &dev->m_wr, &dev->m_util };
  const char *names[3] = { _("Read"), _("Write"), _("Busy") };
  int n = r.w >= 60 ? 3 : 2;
  int gw = r.w / n;

  for (int i = 0; i < n; i++)
    {
      struct rect g = { r.y, r.x + i * gw, r.h, i < n - 1 ? gw : r.w - i * gw };
      char v[32], t[96];
      double last = metric_last (ms[i]);
      if (i < 2)
        fmt_rate (v, sizeof v, last * 1048576);
      else
        fmt_pct (v, sizeof v, last);
      snprintf (t, sizeof t, "%s  %s", names[i], v);
      double scale = i < 2 ? rate_scale (ms[i]) : 100;
      widget_graph_box (cv, g, t, ms[i], scale, i < 2 ? 0 : 100,
                        i < 2 ? fmt_axis_mib : NULL, a->interval);
    }
}

static void
draw_fs (const struct disks *d, struct canvas *cv, int y, int x, int w,
         int h)
{
  const struct table_col cols[] = {
    { _("Mounted on"), 0, false, 0 },
    { _("Type"), 7, false, 2 },
    { _("Device"), 16, false, 1 },
    { _("Size"), 7, true, 0 },
    { _("Used"), 7, true, 3 },
    { _("Free"), 7, true, 0 },
    { _("Use"), USE_BAR + 5, false, 0 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);
  if (d->nfs == 0)
    {
      canvas_put_center (cv, y + 1, x, w, _("Reading file systems..."),
                         ST_DIM);
      return;
    }
  for (int i = 0; i < d->nfs && i < h - 1; i++)
    {
      const struct fs_entry *f = &d->fs[i];
      char size[32], used[32], avail[32], use[64];
      fmt_bytes (size, sizeof size, f->size);
      fmt_bytes (used, sizeof used, f->used);
      fmt_bytes (avail, sizeof avail, f->avail);
      int st = -1;
      double frac = f->size > 0 ? f->used / f->size : NAN;
      if (isnan (frac))
        snprintf (use, sizeof use, "%s", _("n/a"));
      else
        {
          /* A bar of UTF-8 block characters, then the percentage.  */
          int filled = (int) (frac * USE_BAR + 0.5);
          char *p = use;
          for (int k = 0; k < USE_BAR; k++)
            {
              const char *ch = k < filled ? "█" : "░";
              size_t len = strlen (ch);
              memcpy (p, ch, len);
              p += len;
            }
          snprintf (p, use + sizeof use - p, " %3.0f%%", frac * 100);
          st = level_style (frac);
        }
      const char *cells[N] = { f->mount, f->type, f->device, size, used,
                               avail, use };
      int styles[N] = { -1, -1, -1, -1, -1, -1, st };
      table_row (cv, y + 1 + i, x, w, cols, N, widths, cells, styles,
                 ST_BASE);
    }
}

void
page_disk_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct disks *d = &a->disks;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Disks"), ST_TITLE);

  int x = r.x + 1, w = r.w - 2, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  if (d->n == 0)
    canvas_put_center (cv, y + 1, x, w, _("No disks found."), ST_LABEL);
  else
    {
      if (a->disk_sel >= (size_t) d->n)
        a->disk_sel = d->n - 1;
      const struct disk_dev *dev = &d->dev[a->disk_sel];
      int rows = d->n < LIST_ROWS ? d->n : LIST_ROWS;
      draw_list (a, cv, y, x + 1, w - 2, rows);
      y += 1 + rows;

      char head[160];
      if (dev->model[0])
        snprintf (head, sizeof head, "%s: %s", dev->name, dev->model);
      else
        snprintf (head, sizeof head, "%s", dev->name);
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, head, ST_TITLE);
      draw_details (dev, cv, y + 1, x + 1, x + w - 1);
      y += 2;

      int rest = bottom - y;
      int fs_h = 0;
      if (rest >= GRAPH_MIN + FS_MIN)
        {
          int want = 2 + (d->nfs > 0 ? d->nfs : 1);
          fs_h = rest / 2 < want ? rest / 2 : want;
          if (fs_h < FS_MIN)
            fs_h = FS_MIN;
        }
      int graph_h = rest - fs_h;
      if (graph_h >= GRAPH_MIN)
        draw_graphs (a, cv, (struct rect) { y, x, graph_h, w }, dev);
      else
        fs_h = rest;
      y = bottom - fs_h;
    }

  if (bottom - y >= FS_MIN)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("File systems"), ST_TITLE);
      draw_fs (d, cv, y + 1, x + 1, w - 2, bottom - y - 1);
    }
}

bool
page_disk_key (struct app *a, int ch)
{
  size_t n = (size_t) a->disks.n;
  if (n == 0)
    return false;
  switch (ch)
    {
    case KEY_UP:
      if (a->disk_sel > 0)
        a->disk_sel--;
      return true;
    case KEY_DOWN:
      if (a->disk_sel + 1 < n)
        a->disk_sel++;
      return true;
    case KEY_HOME:
      a->disk_sel = 0;
      return true;
    case KEY_END:
      a->disk_sel = n - 1;
      return true;
    default:
      return false;
    }
}

/* page_memory.c -- the Memory page.
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
#include "memory.h"
#include "pages.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

enum
{
  VALUE_ROWS = 7,
  COLUMN_MIN = 30,		/* Width of a column of values.  */
  GRAPH_MIN = 5,
  PROC_MIN = 4			/* Divider, header and two rows.  */
};

static double
ratio (double part, double whole)
{
  return whole > 0 ? part / whole : NAN;
}

static void
draw_bars (const struct memory *m, struct canvas *cv, int y, int x, int w)
{
  const char *labels[] = {
    _("programs"), _("kernel"), _("buffers"), _("cache"), _("free")
  };
  double parts[] = { m->apps, m->kernel, m->buffers, m->cached, m->free };
  static const int styles[] = { ST_OK, ST_ACCENT, ST_FRAME, ST_WARN, ST_DIM };
  enum { N = ARRAY_CARDINALITY (styles) };
  double fracs[N];
  char used[64], swap[64];

  /* Free memory is the empty part of the bar.  */
  for (int i = 0; i < N; i++)
    fracs[i] = i < N - 1 ? ratio (parts[i], m->total) : 0;

  const char *ml = _("Memory"), *sl = _("Swap");
  int lw = text_width (ml) > text_width (sl) ? text_width (ml) : text_width (sl);
  fmt_used_total (used, sizeof used, m->used, m->total);
  if (m->swap_total > 0)
    fmt_used_total (swap, sizeof swap, m->swap_used, m->swap_total);
  else
    snprintf (swap, sizeof swap, "%s", _("no swap"));
  int tw = text_width (used) > text_width (swap) ? text_width (used)
                                                 : text_width (swap);
  int bw = w - lw - 1 - tw - 2;
  if (bw < 4)
    return;

  canvas_put (cv, y, x, lw, ml, ST_LABEL);
  widget_stacked_bar (cv, y, x + lw + 1, bw, fracs, styles, N);
  canvas_put_right (cv, y, x + w - tw, tw, used, ST_VALUE);

  int cx = x + lw + 1;
  for (int i = 0; i < N; i++)
    {
      char v[32];
      fmt_mib (v, sizeof v, parts[i]);
      cx = widget_legend (cv, y + 1, cx, x + w, styles[i], labels[i], v);
    }

  /* Swap: the same layout with a plain bar.  */
  canvas_put (cv, y + 2, x, lw, sl, ST_LABEL);
  double sf = ratio (m->swap_used, m->swap_total);
  int st = isnan (sf) ? ST_DIM : level_style (sf);
  widget_stacked_bar (cv, y + 2, x + lw + 1, bw, &sf, &st, 1);
  canvas_put_right (cv, y + 2, x + w - tw, tw, swap, ST_VALUE);
}

static void
draw_values (const struct memory *m, struct canvas *cv, int y, int x, int w)
{
  char v[3][VALUE_ROWS][64];
  const char *l[3][VALUE_ROWS] = {
    { _("Total"), _("Used"), _("Available"), _("Shared"), _("Buffers"),
      _("Cache"), _("Kernel") },
    { _("Swap cache"), _("zram data"), _("zram size"), _("Dirty"),
      _("Writeback"), _("Committed"), _("Huge pages") },
    { _("Page faults"), _("Major faults"), _("Swapped in"),
      _("Swapped out"), _("Waiting"), _("Stalled"), "" },
  };

  fmt_mib (v[0][0], 64, m->total);
  fmt_mib (v[0][1], 64, m->used);
  fmt_mib (v[0][2], 64, m->available);
  fmt_mib (v[0][3], 64, m->shared);
  fmt_mib (v[0][4], 64, m->buffers);
  fmt_mib (v[0][5], 64, m->cached);
  fmt_mib (v[0][6], 64, m->kernel);

  fmt_mib (v[1][0], 64, m->swap_cached);
  fmt_mib (v[1][1], 64, m->zram_orig);
  if (m->zram_compr > 0)
    {
      char c[32];
      fmt_mib (c, sizeof c, m->zram_compr);
      snprintf (v[1][2], 64, _("%s (%.1f:1)"), c, m->zram_orig / m->zram_compr);
    }
  else
    fmt_mib (v[1][2], 64, m->zram_compr);
  fmt_mib (v[1][3], 64, m->dirty);
  fmt_mib (v[1][4], 64, m->writeback);
  fmt_used_total (v[1][5], 64, m->committed, m->commit_limit);
  if (isnan (m->huge_total))
    snprintf (v[1][6], 64, "%s", _("n/a"));
  else if (m->huge_total == 0)
    snprintf (v[1][6], 64, "%s", _("none"));
  else
    snprintf (v[1][6], 64, _("%.0f of %.0f free"), m->huge_free, m->huge_total);

  fmt_count_rate (v[2][0], 64, m->faults);
  fmt_count_rate (v[2][1], 64, m->major_faults);
  fmt_count_rate (v[2][2], 64, m->swap_in);
  fmt_count_rate (v[2][3], 64, m->swap_out);
  fmt_pct (v[2][4], 64, m->psi_some);
  fmt_pct (v[2][5], 64, m->psi_full);
  v[2][6][0] = '\0';

  int cols = w / COLUMN_MIN;
  if (cols > 3)
    cols = 3;
  if (cols < 1)
    cols = 1;
  int cw = w / cols;
  for (int c = 0; c < cols; c++)
    {
      const char *vals[VALUE_ROWS];
      for (int i = 0; i < VALUE_ROWS; i++)
        vals[i] = v[c][i];
      widget_values (cv, y, x + c * cw, cw - 2, l[c], vals, VALUE_ROWS);
    }
}

static void
draw_top (const struct memory *m, struct canvas *cv, int y, int x, int w,
          int h)
{
  const struct table_col cols[] = {
    { _("PID"), 8, true, 0 },
    { _("Memory"), 10, true, 0 },
    { _("Share"), 6, true, 1 },
    { _("Command"), 0, false, 0 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);
  if (!m->top_valid)
    {
      canvas_put_center (cv, y + 1, x, w, _("Reading processes..."), ST_DIM);
      return;
    }
  for (int i = 0; i < m->ntop && i < h - 1; i++)
    {
      char pid[16], rss[32], share[16];
      snprintf (pid, sizeof pid, "%u", m->top[i].pid);
      fmt_mib (rss, sizeof rss, m->top[i].rss);
      fmt_pct (share, sizeof share, 100 * ratio (m->top[i].rss, m->total));
      const char *cells[N] = { pid, rss, share, m->top[i].name };
      table_row (cv, y + 1 + i, x, w, cols, N, widths, cells, NULL, ST_BASE);
    }
}

void
page_memory_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct memory *m = &a->mem;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Memory"), ST_TITLE);

  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  draw_bars (m, cv, y, x, w);
  y += 3;
  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, _("Details"), ST_TITLE);
  y++;
  int rows = bottom - y < VALUE_ROWS ? bottom - y : VALUE_ROWS;
  if (rows == VALUE_ROWS)
    {
      draw_values (m, cv, y, x, w);
      y += VALUE_ROWS;
    }

  int rest = bottom - y;
  int proc_h = 0;
  if (rest >= GRAPH_MIN + PROC_MIN)
    {
      int want = 2 + (m->ntop > 0 ? m->ntop : 1);
      proc_h = rest / 2 < want ? rest / 2 : want;
      if (proc_h < PROC_MIN)
        proc_h = PROC_MIN;
    }
  int graph_h = rest - proc_h;
  if (graph_h >= GRAPH_MIN)
    {
      struct rect g1 = { y, r.x + 1, graph_h, (r.w - 2) / 2 };
      struct rect g2 = { y, g1.x + g1.w, graph_h, r.w - 2 - g1.w };
      char t1[96], t2[96], v[32];
      fmt_pct (v, sizeof v, metric_last (&m->m_used));
      snprintf (t1, sizeof t1, "%s  %s", _("Used"), v);
      fmt_pct (v, sizeof v, metric_last (&m->m_swap));
      snprintf (t2, sizeof t2, "%s  %s", _("Swap used"), v);
      widget_graph_box (cv, g1, t1, &m->m_used, 100, 100, NULL, a->interval);
      widget_graph_box (cv, g2, t2, &m->m_swap, 100, 100, NULL, a->interval);
    }
  else
    proc_h = rest;
  y = bottom - proc_h;
  if (proc_h >= PROC_MIN)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("Largest processes"), ST_TITLE);
      draw_top (m, cv, y + 1, x, w, proc_h - 1);
    }
}

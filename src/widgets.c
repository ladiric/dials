/* widgets.c -- meters, graphs and dialogs drawn on a canvas.
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

#include "theme.h"
#include "util.h"
#include "widgets.h"

/* Bars in eighths of a cell.  */
static const wchar_t blocks[9] = {
  L' ', L'▁', L'▂', L'▃', L'▄', L'▅', L'▆', L'▇', L'█'
};

int
level_style (double frac)
{
  return frac < 0.6 ? ST_OK : frac < 0.85 ? ST_WARN : ST_CRIT;
}

void
fmt_compact (char *buf, size_t size, double v)
{
  if (v >= 10000)
    snprintf (buf, size, "%.0fk", v / 1000);
  else if (v >= 1000)
    snprintf (buf, size, "%.1fk", v / 1000);
  else
    snprintf (buf, size, "%.0f", v);
}

void
fmt_axis_mib (char *buf, size_t size, double mib)
{
  if (mib >= 10 * 1024)
    snprintf (buf, size, "%.0fG", mib / 1024);
  else if (mib >= 1024)
    snprintf (buf, size, "%.1fG", mib / 1024);
  else if (mib >= 1 || mib == 0)
    snprintf (buf, size, "%.0fM", mib);
  else
    snprintf (buf, size, "%.0fK", mib * 1024);
}

void
widget_meter (struct canvas *cv, int y, int x, int w, const char *label,
              int label_w, double frac, const char *text, int text_w)
{
  enum { PCT_W = 5 };		/* " 100%" */
  canvas_put (cv, y, x, label_w, label, ST_LABEL);

  int bx = x + label_w + 1;
  int bw = w - label_w - 1 - PCT_W - (text_w > 0 ? text_w + 2 : 0);
  if (bw < 4)
    {
      text = NULL;
      bw = w - label_w - 1 - PCT_W;
    }
  if (bw < 1)
    return;

  if (isnan (frac))
    canvas_put (cv, y, bx, bw, _("n/a"), ST_DIM);
  else
    {
      if (frac < 0)
        frac = 0;
      if (frac > 1)
        frac = 1;
      int filled = (int) (frac * bw + 0.5);
      canvas_repeat (cv, y, bx, filled, L'█', level_style (frac));
      canvas_repeat (cv, y, bx + filled, bw - filled, L'░', ST_DIM);
      char pct[16];
      snprintf (pct, sizeof pct, " %3.0f%%", frac * 100);
      canvas_put (cv, y, bx + bw, PCT_W, pct, ST_VALUE);
    }
  if (text)
    canvas_put (cv, y, bx + bw + PCT_W + 2, text_w, text, ST_VALUE);
}

void
widget_graph (struct canvas *cv, struct rect r, const struct metric *m,
              double scale, double cbase,
              void (*axis) (char *, size_t, double))
{
  enum { LAB = 5 };		/* Axis labels and the axis line.  */
  char buf[16];

  if (r.h < 1 || r.w < LAB + 2)
    return;
  if (!(scale > 0))
    scale = 100;
  if (!axis)
    axis = fmt_compact;
  int pw = r.w - LAB;

  axis (buf, sizeof buf, scale);
  canvas_put_right (cv, r.y, r.x, LAB - 1, buf, ST_LABEL);
  if (r.h > 2)
    {
      axis (buf, sizeof buf, scale / 2);
      canvas_put_right (cv, r.y + r.h / 2, r.x, LAB - 1, buf, ST_LABEL);
      /* A faint middle line, so that an empty graph still reads as one.  */
      canvas_repeat (cv, r.y + r.h / 2, r.x + LAB, pw, L'·', ST_DIM);
    }
  canvas_put_right (cv, r.y + r.h - 1, r.x, LAB - 1, "0", ST_LABEL);
  for (int row = 0; row < r.h; row++)
    canvas_putwc (cv, r.y + row, r.x + LAB - 1, L'│', ST_FRAME);

  size_t n = m->len;
  size_t show = n < (size_t) pw ? n : (size_t) pw;
  for (size_t i = 0; i < show; i++)
    {
      double v = metric_at (m, n - show + i);
      if (isnan (v))
        continue;
      int col = r.x + LAB + (pw - (int) show) + (int) i;
      double frac = v / scale;
      if (frac < 0)
        frac = 0;
      if (frac > 1)
        frac = 1;
      int eighths = (int) (frac * r.h * 8 + 0.5);
      int st = level_style (cbase > 0 ? v / cbase : 0);
      for (int row = 0; row < r.h; row++)
        {
          int fill = eighths - row * 8;
          if (fill <= 0)
            break;
          canvas_putwc (cv, r.y + r.h - 1 - row, col,
                        blocks[fill > 8 ? 8 : fill], st);
        }
    }
}

void
widget_graph_box (struct canvas *cv, struct rect r, const char *title,
                  const struct metric *m, double scale, double cbase,
                  void (*axis) (char *, size_t, double), double interval)
{
  if (r.h < 3 || r.w < 12)
    return;
  canvas_box (cv, r, ST_FRAME, false);
  canvas_title (cv, r.y, r.x, r.w, title, ST_TITLE);
  struct rect in = { r.y + 1, r.x + 1, r.h - 2, r.w - 2 };
  widget_graph (cv, in, m, scale, cbase, axis);

  char span[32], lab[40];
  fmt_duration (span, sizeof span, (r.w - 2 - 5) * interval);
  snprintf (lab, sizeof lab, " -%s ", span);
  int bottom = r.y + r.h - 1;
  canvas_put (cv, bottom, r.x + 5, r.w / 2 - 5, lab, ST_LABEL);
  char now[32];
  snprintf (now, sizeof now, " %s ", _("now"));
  canvas_put_right (cv, bottom, r.x + r.w / 2, r.w / 2 - 2, now, ST_LABEL);
}

void
widget_stat_block (struct canvas *cv, struct rect r, const char *title,
                   const char *value, double frac, int st,
                   const struct metric *m, void (*fmt) (char *, size_t, double))
{
  enum { PCT_W = 5 };

  if (r.h < 4 || r.w < 12)
    return;
  canvas_box (cv, r, ST_FRAME, false);
  canvas_title (cv, r.y, r.x, r.w, title, ST_TITLE);
  int x = r.x + 1, w = r.w - 2;

  canvas_put_center (cv, r.y + 1, x, w, value, st);

  int bw = w - 2 - PCT_W;
  if (isnan (frac))
    canvas_put_center (cv, r.y + 2, x, w, _("n/a"), ST_DIM);
  else if (bw >= 3)
    {
      if (frac < 0)
        frac = 0;
      if (frac > 1)
        frac = 1;
      int filled = (int) (frac * bw + 0.5);
      canvas_repeat (cv, r.y + 2, x + 1, filled, L'█', st);
      canvas_repeat (cv, r.y + 2, x + 1 + filled, bw - filled, L'░', ST_DIM);
      char pct[16];
      snprintf (pct, sizeof pct, " %3.0f%%", frac * 100);
      canvas_put (cv, r.y + 2, x + 1 + bw, PCT_W, pct, ST_VALUE);
    }

  double lo, avg, hi;
  if (r.h >= 5 && metric_stats (m, &lo, &avg, &hi))
    {
      char a1[32], a2[32], a3[32], line[160];
      fmt (a1, sizeof a1, lo);
      fmt (a2, sizeof a2, avg);
      fmt (a3, sizeof a3, hi);
      snprintf (line, sizeof line, _("min %s  avg %s  max %s"), a1, a2, a3);
      if (text_width (line) > w - 2)
        snprintf (line, sizeof line, _("min %s  max %s"), a1, a3);
      if (text_width (line) > w - 2)
        snprintf (line, sizeof line, _("max %s"), a3);
      canvas_put_center (cv, r.y + 3, x, w, line, ST_LABEL);
    }
}

int
widget_pair (struct canvas *cv, int y, int x, int end, const char *label,
             const char *value)
{
  int need = text_width (label) + 1 + text_width (value);
  if (x + need > end)
    return x;
  x += canvas_put (cv, y, x, -1, label, ST_LABEL) + 1;
  x += canvas_put (cv, y, x, -1, value, ST_VALUE);
  return x + 3;
}

int
widget_legend (struct canvas *cv, int y, int x, int end, int st,
               const char *label, const char *value)
{
  int need = 2 + text_width (label) + 1 + text_width (value);
  if (x + need > end)
    return x;
  canvas_putwc (cv, y, x, L'█', st);
  return widget_pair (cv, y, x + 2, end, label, value);
}

void
widget_stacked_bar (struct canvas *cv, int y, int x, int w,
                    const double *fracs, const int *styles, int n)
{
  double edge = 0;
  int col = 0;
  for (int i = 0; i < n; i++)
    {
      if (isnan (fracs[i]) || fracs[i] <= 0)
        continue;
      /* Round the running total, so that the parts add up to the
         whole.  */
      edge += fracs[i];
      int to = (int) (edge * w + 0.5);
      if (to > w)
        to = w;
      canvas_repeat (cv, y, x + col, to - col, L'█', styles[i]);
      if (to > col)
        col = to;
    }
  canvas_repeat (cv, y, x + col, w - col, L'░', ST_DIM);
}

void
widget_values (struct canvas *cv, int y, int x, int w,
               const char *const *labels, const char *const *values, int n)
{
  int lw = 0;
  for (int i = 0; i < n; i++)
    {
      int t = text_width (labels[i]);
      if (t > lw)
        lw = t;
    }
  if (lw > w / 2)
    lw = w / 2;
  for (int i = 0; i < n; i++)
    {
      canvas_put (cv, y + i, x, lw, labels[i], ST_LABEL);
      canvas_put (cv, y + i, x + lw + 2, w - lw - 2, values[i], ST_VALUE);
    }
}

void
table_layout (const struct table_col *cols, int n, int w, int *widths)
{
  enum { FLEX_MIN = 8 };
  int maxdrop = 0;

  for (int i = 0; i < n; i++)
    if (cols[i].drop > maxdrop)
      maxdrop = cols[i].drop;

  /* Columns whose drop number is at least LEVEL are hidden.  Lower the
     level until the shown columns fit.  */
  for (int level = maxdrop + 1;; level--)
    {
      int used = 0, nflex = 0, shown = 0;
      for (int i = 0; i < n; i++)
        {
          if (cols[i].drop && cols[i].drop >= level)
            continue;
          shown++;
          if (cols[i].width)
            used += cols[i].width;
          else
            nflex++;
        }
      int space = w - used - (shown > 0 ? shown - 1 : 0);
      bool fits = nflex ? space >= nflex * FLEX_MIN : space >= 0;
      if (fits || level <= 1)
        {
          int flex_left = nflex;
          for (int i = 0; i < n; i++)
            {
              if (cols[i].drop && cols[i].drop >= level)
                widths[i] = 0;
              else if (cols[i].width)
                widths[i] = cols[i].width;
              else
                {
                  int fw = space / flex_left;
                  widths[i] = fw > 0 ? fw : 0;
                  space -= fw;
                  flex_left--;
                }
            }
          return;
        }
    }
}

void
table_row (struct canvas *cv, int y, int x, int w,
           const struct table_col *cols, int n, const int *widths,
           const char *const *cells, const int *styles, int row_st)
{
  int end = x + w, cx = x;

  canvas_fill (cv, (struct rect) { y, x, 1, w }, row_st);
  for (int i = 0; i < n && cx < end; i++)
    {
      if (widths[i] == 0)
        continue;
      int cw = widths[i] < end - cx ? widths[i] : end - cx;
      int st = styles && styles[i] >= 0 ? styles[i] : row_st;
      if (cols[i].right)
        canvas_put_right (cv, y, cx, cw, cells[i], st);
      else
        canvas_put (cv, y, cx, cw, cells[i], st);
      cx += widths[i] + 1;
    }
}

void
widget_dialog (struct canvas *cv, const char *title,
               const char *const *lines, int n, bool center)
{
  int tw = text_width (title) + 4;
  for (int i = 0; i < n; i++)
    {
      int lw = text_width (lines[i]);
      if (lw > tw)
        tw = lw;
    }
  int w = tw + 6, h = n + 4;
  if (w > cv->w)
    w = cv->w;
  if (h > cv->h)
    h = cv->h;
  struct rect r = { (cv->h - h) / 2, (cv->w - w) / 2, h, w };

  canvas_fill (cv, r, ST_DLG);
  canvas_box (cv, r, ST_DLG_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, title, ST_DLG_TITLE);
  for (int i = 0; i < n && i < h - 4; i++)
    {
      if (center)
        canvas_put_center (cv, r.y + 2 + i, r.x + 3, w - 6, lines[i], ST_DLG);
      else
        canvas_put (cv, r.y + 2 + i, r.x + 3, w - 6, lines[i], ST_DLG);
    }
}

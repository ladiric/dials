/* page_net.c -- the Network page.
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
#include "net.h"
#include "pages.h"
#include "theme.h"
#include "tui.h"
#include "util.h"
#include "widgets.h"

enum
{
  LIST_ROWS = 8,		/* Interfaces shown before the list scrolls.  */
  GRAPH_MIN = 5
};

static void
fmt_speed (char *buf, size_t size, double mbps)
{
  if (isnan (mbps))
    snprintf (buf, size, "%s", "");
  else if (mbps >= 1000)
    snprintf (buf, size, "%g Gb/s", mbps / 1000);
  else
    snprintf (buf, size, "%g Mb/s", mbps);
}

static void
draw_list (struct app *a, struct canvas *cv, int y, int x, int w, int rows)
{
  const struct net *n = &a->net;
  const struct table_col cols[] = {
    { "", 1, false, 0 },
    { _("Interface"), 0, false, 0 },
    { _("State"), 8, false, 1 },
    { _("Speed"), 9, true, 2 },
    { _("Receive"), 11, true, 0 },
    { _("Send"), 11, true, 0 },
    { _("Received"), 9, true, 3 },
    { _("Sent"), 9, true, 3 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);

  int first = (int) a->net_sel >= rows ? (int) a->net_sel - rows + 1 : 0;
  for (int row = 0; row < rows && first + row < n->n; row++)
    {
      int i = first + row;
      const struct net_if *d = &n->dev[i];
      bool sel = (size_t) i == a->net_sel;
      char speed[24], rx[32], tx[32], rxt[32], txt[32];
      fmt_speed (speed, sizeof speed, d->speed);
      fmt_rate (rx, sizeof rx, d->rx_bps);
      fmt_rate (tx, sizeof tx, d->tx_bps);
      fmt_bytes (rxt, sizeof rxt, d->rx_total);
      fmt_bytes (txt, sizeof txt, d->tx_total);
      const char *cells[N] = { sel ? "►" : "", d->name, d->state, speed, rx,
                               tx, rxt, txt };
      int styles[N] = { -1, -1, -1, -1, -1, -1, -1, -1 };
      if (!sel)
        styles[2] = streq (d->state, "up") ? ST_OK
          : streq (d->state, "down") ? ST_DIM : -1;
      table_row (cv, y + 1 + row, x, w, cols, N, widths, cells, styles,
                 sel ? ST_SEL : ST_BASE);
    }
}

static void
draw_details (const struct net_if *d, struct canvas *cv, int y, int x,
              int end)
{
  char b[64];
  int cx = x;
  if (d->mac[0])
    cx = widget_pair (cv, y, cx, end, _("Hardware address"), d->mac);
  if (!isnan (d->mtu))
    {
      snprintf (b, sizeof b, "%.0f", d->mtu);
      cx = widget_pair (cv, y, cx, end, _("MTU"), b);
    }
  fmt_count_rate (b, sizeof b, d->rx_pps);
  cx = widget_pair (cv, y, cx, end, _("Packets in"), b);
  fmt_count_rate (b, sizeof b, d->tx_pps);
  cx = widget_pair (cv, y, cx, end, _("out"), b);
  fmt_count_rate (b, sizeof b, d->errors);
  cx = widget_pair (cv, y, cx, end, _("Errors"), b);
  fmt_count_rate (b, sizeof b, d->drops);
  widget_pair (cv, y, cx, end, _("Dropped"), b);

  cx = x;
  if (d->naddr == 0)
    widget_pair (cv, y + 1, cx, end, _("Addresses"), _("none"));
  for (int i = 0; i < d->naddr; i++)
    cx = widget_pair (cv, y + 1, cx, end, i == 0 ? _("Addresses") : "",
                      d->addr[i]);
}

static double
rate_scale (const struct metric *m)
{
  double lo, avg, hi;
  if (!metric_stats (m, &lo, &avg, &hi) || hi < 0.01)
    return 0.01;		/* 10 KiB/s, so that a quiet link shows.  */
  return hi * 1.1;
}

static void
draw_conns (const struct net_conns *c, struct canvas *cv, int y, int x,
            int end)
{
  if (!c->valid)
    return;
  const char *labels[] = {
    _("Established"), _("Listening"), _("Opening"), _("Closing"),
    _("Time-wait"), _("Close-wait"), _("UDP")
  };
  double values[] = { c->established, c->listening, c->opening, c->closing,
                      c->time_wait, c->close_wait, c->udp };
  int cx = x;
  for (size_t i = 0; i < ARRAY_CARDINALITY (labels); i++)
    {
      char b[32];
      snprintf (b, sizeof b, "%.0f", values[i]);
      cx = widget_pair (cv, y, cx, end, labels[i], b);
    }
}

void
page_net_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct net *n = &a->net;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Network"), ST_TITLE);
  int x = r.x + 1, w = r.w - 2, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  if (n->n == 0)
    {
      canvas_put_center (cv, y + 1, x, w, _("No network interfaces found."),
                         ST_LABEL);
      return;
    }
  if (a->net_sel >= (size_t) n->n)
    a->net_sel = n->n - 1;
  const struct net_if *d = &n->dev[a->net_sel];

  int rows = n->n < LIST_ROWS ? n->n : LIST_ROWS;
  if (rows > (bottom - y) / 3)
    rows = (bottom - y) / 3 > 1 ? (bottom - y) / 3 : 1;
  draw_list (a, cv, y, x + 1, w - 2, rows);
  y += 1 + rows;

  canvas_divider (cv, y, r.x, r.w, ST_FRAME);
  canvas_title (cv, y, r.x, r.w, d->name, ST_TITLE);
  draw_details (d, cv, y + 1, x + 1, x + w - 1);
  y += 3;

  /* The connections line at the bottom, the graphs above it.  */
  int conn_h = n->conns.valid ? 2 : 0;
  int graph_h = bottom - y - conn_h;
  if (graph_h >= GRAPH_MIN)
    {
      struct rect g1 = { y, x, graph_h, w / 2 };
      struct rect g2 = { y, x + g1.w, graph_h, w - g1.w };
      char t1[96], t2[96], v[32];
      fmt_rate (v, sizeof v, d->rx_bps);
      snprintf (t1, sizeof t1, "%s  %s", _("Receive"), v);
      fmt_rate (v, sizeof v, d->tx_bps);
      snprintf (t2, sizeof t2, "%s  %s", _("Send"), v);
      widget_graph_box (cv, g1, t1, &d->m_rx, rate_scale (&d->m_rx), 0,
                        fmt_axis_mib, a->interval);
      widget_graph_box (cv, g2, t2, &d->m_tx, rate_scale (&d->m_tx), 0,
                        fmt_axis_mib, a->interval);
      y += graph_h;
    }
  if (conn_h && bottom - y >= conn_h)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("Connections"), ST_TITLE);
      draw_conns (&n->conns, cv, y + 1, x + 1, x + w - 1);
    }
}

bool
page_net_key (struct app *a, int ch)
{
  size_t n = (size_t) a->net.n;
  if (n == 0)
    return false;
  switch (ch)
    {
    case KEY_UP:
      if (a->net_sel > 0)
        a->net_sel--;
      return true;
    case KEY_DOWN:
      if (a->net_sel + 1 < n)
        a->net_sel++;
      return true;
    case KEY_HOME:
      a->net_sel = 0;
      return true;
    case KEY_END:
      a->net_sel = n - 1;
      return true;
    default:
      return false;
    }
}

/* page_power.c -- the Power page.
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
#include "power.h"
#include "sensors.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

enum { GRAPH_MIN = 5 };

static void
fmt_hours (char *buf, size_t size, double seconds)
{
  if (isnan (seconds))
    snprintf (buf, size, "%s", "");
  else
    {
      long m = (long) (seconds / 60 + 0.5);
      snprintf (buf, size, "%ld:%02ld", m / 60, m % 60);
    }
}

/* Return a translated description of a battery's status.  */
static const char *
status_text (const struct battery *b)
{
  if (strcmp (b->status, "Charging") == 0)
    return _("charging");
  if (strcmp (b->status, "Discharging") == 0)
    return _("discharging");
  if (strcmp (b->status, "Full") == 0)
    return _("full");
  if (strcmp (b->status, "Not charging") == 0)
    return _("not charging");
  return b->status[0] ? b->status : _("unknown");
}

static int
draw_batteries (const struct power *p, struct canvas *cv, int y, int x, int w,
                int bottom)
{
  if (p->nbat == 0)
    {
      canvas_put (cv, y, x, w, _("No batteries."), ST_DIM);
      return 1;
    }
  int rows = 0;
  for (int i = 0; i < p->nbat && y + rows < bottom; i++)
    {
      const struct battery *b = &p->bat[i];
      int cy = y + rows;
      const char *name = b->model[0] ? b->model : b->name;
      int nw = w / 3;
      canvas_put (cv, cy, x, nw - 1, name, ST_VALUE);

      /* A bar for the charge, or the level a device reports instead.  */
      int bx = x + nw, bw = w / 4;
      char pct[32];
      if (!isnan (b->capacity))
        {
          double f = b->capacity / 100;
          int st = f < 0.15 ? ST_CRIT : f < 0.3 ? ST_WARN : ST_OK;
          int filled = (int) (f * bw + 0.5);
          canvas_repeat (cv, cy, bx, filled, L'█', st);
          canvas_repeat (cv, cy, bx + filled, bw - filled, L'░', ST_DIM);
          snprintf (pct, sizeof pct, " %3.0f%%", b->capacity);
        }
      else
        snprintf (pct, sizeof pct, "%s", b->level[0] ? b->level : _("n/a"));
      int px = isnan (b->capacity) ? bx : bx + bw;
      int cx = px + canvas_put (cv, cy, px, -1, pct, ST_VALUE) + 2;

      char v[64];
      cx = widget_pair (cv, cy, cx, x + w, "", status_text (b));
      fmt_hours (v, sizeof v, b->time_left);
      if (v[0])
        cx = widget_pair (cv, cy, cx, x + w,
                          strcmp (b->status, "Charging") == 0
                          ? _("Full in") : _("Left"), v);
      if (!isnan (b->power) && b->power > 0)
        {
          snprintf (v, sizeof v, "%.1f W", b->power);
          cx = widget_pair (cv, cy, cx, x + w, _("Rate"), v);
        }
      rows++;

      /* The system's battery gets a second line about its wear.  */
      if (!b->device && y + rows < bottom
          && (!isnan (b->energy_full) || !isnan (b->cycles)))
        {
          cy = y + rows;
          cx = x + 2;
          if (b->energy_design > 0 && !isnan (b->energy_full))
            {
              snprintf (v, sizeof v, _("%.0f%% (%.1f of %.1f Wh)"),
                        100 * b->energy_full / b->energy_design,
                        b->energy_full, b->energy_design);
              cx = widget_pair (cv, cy, cx, x + w, _("Health"), v);
            }
          if (!isnan (b->cycles))
            {
              snprintf (v, sizeof v, "%.0f", b->cycles);
              cx = widget_pair (cv, cy, cx, x + w, _("Cycles"), v);
            }
          if (!isnan (b->voltage))
            {
              snprintf (v, sizeof v, "%.2f V", b->voltage);
              cx = widget_pair (cv, cy, cx, x + w, _("Voltage"), v);
            }
          if (b->technology[0])
            widget_pair (cv, cy, cx, x + w, _("Type"), b->technology);
          rows++;
        }
    }
  return rows;
}

static int
draw_rapl (const struct power *p, struct canvas *cv, int y, int x, int w)
{
  if (p->nrapl == 0)
    {
      canvas_put (cv, y, x, w, p->rapl_denied
                  ? _("Reading processor power needs root privileges.")
                  : _("This system does not report processor power."),
                  ST_DIM);
      return 1;
    }
  int cx = x;
  for (int i = 0; i < p->nrapl; i++)
    {
      char v[32];
      if (isnan (p->rapl[i].watts))
        snprintf (v, sizeof v, "%s", _("n/a"));
      else
        snprintf (v, sizeof v, "%.1f W", p->rapl[i].watts);
      int next = widget_pair (cv, y, cx, x + w, p->rapl[i].name, v);
      if (next == cx)
        break;
      cx = next;
    }
  return 1;
}

/* Power readings from hardware monitoring, such as a power supply's
   output.  Return the rows used.  */
static int
draw_power_sensors (const struct sensors *s, struct canvas *cv, int y, int x,
                    int w, int bottom)
{
  int rows = 0, cx = x;
  for (int i = 0; i < s->n && y + rows < bottom; i++)
    {
      const struct sensor *sn = &s->s[i];
      if (sn->kind != SENSOR_POWER)
        continue;
      char label[128], v[32];
      snprintf (label, sizeof label, "%s %s", sn->chip, sn->label);
      fmt_sensor (v, sizeof v, sn->kind, sn->value);
      int next = widget_pair (cv, y + rows, cx, x + w, label, v);
      if (next == cx)
        {
          /* Full line: go on with the next one.  */
          rows++;
          cx = x;
          if (y + rows >= bottom)
            break;
          next = widget_pair (cv, y + rows, cx, x + w, label, v);
        }
      cx = next;
    }
  return cx > x ? rows + 1 : rows;
}

static const struct sensor *
first_power_sensor (const struct sensors *s)
{
  for (int i = 0; i < s->n; i++)
    if (s->s[i].kind == SENSOR_POWER)
      return &s->s[i];
  return NULL;
}

void
page_power_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct power *p = &a->power;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Power"), ST_TITLE);
  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  y += draw_batteries (p, cv, y, x, w, bottom);

  if (p->nsup > 0 && y < bottom)
    {
      int cx = x;
      for (int i = 0; i < p->nsup; i++)
        cx = widget_pair (cv, y, cx, x + w, p->sup[i].name,
                          p->sup[i].online ? _("connected")
                          : _("not connected"));
      y++;
    }

  if (y + 2 <= bottom)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("Processor power"), ST_TITLE);
      y += 1 + draw_rapl (p, cv, y + 1, x, w);
    }

  const struct sensor *ps = first_power_sensor (&a->sensors);
  if (ps && y + 2 <= bottom)
    {
      canvas_divider (cv, y, r.x, r.w, ST_FRAME);
      canvas_title (cv, y, r.x, r.w, _("Power sensors"), ST_TITLE);
      y += 1 + draw_power_sensors (&a->sensors, cv, y + 1, x, w, bottom);
    }

  /* A graph of the package power, else of a power sensor, else of the
     system battery.  */
  const struct metric *m = NULL;
  char t[96], v[32];
  double scale = 100;
  if (p->nrapl > 0)
    {
      double lo, avg, hi;
      m = &p->rapl[0].m;
      scale = metric_stats (m, &lo, &avg, &hi) && hi > 0 ? hi * 1.2 : 10;
      snprintf (v, sizeof v, "%.1f W", metric_last (m));
      snprintf (t, sizeof t, "%s  %s", p->rapl[0].name, v);
    }
  else if (ps)
    {
      double lo, avg, hi;
      m = &ps->m;
      scale = !isnan (ps->limit) ? ps->limit
        : metric_stats (m, &lo, &avg, &hi) && hi > 0 ? hi * 1.2 : 10;
      fmt_sensor (v, sizeof v, ps->kind, ps->value);
      snprintf (t, sizeof t, "%s %s  %s", ps->chip, ps->label, v);
    }
  else
    for (int i = 0; i < p->nbat && !m; i++)
      if (!p->bat[i].device)
        {
          m = &p->bat[i].m_capacity;
          fmt_pct (v, sizeof v, metric_last (m));
          snprintf (t, sizeof t, "%s  %s", _("Charge"), v);
        }
  if (m && bottom - y >= GRAPH_MIN)
    widget_graph_box (cv, (struct rect) { y, r.x + 1, bottom - y, r.w - 2 },
                      t, m, scale, 0, NULL, a->interval);
}

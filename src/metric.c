/* metric.c -- fixed-size history of one measured value.
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

#include "metric.h"
#include "util.h"

void
metric_init (struct metric *m, size_t cap)
{
  m->buf = xcalloc (cap, sizeof *m->buf);
  m->cap = cap;
  m->len = m->head = 0;
}

void
metric_free (struct metric *m)
{
  free (m->buf);
  m->buf = NULL;
  m->cap = m->len = m->head = 0;
}

void
metric_clear (struct metric *m)
{
  m->len = m->head = 0;
}

void
metric_push (struct metric *m, double v)
{
  m->buf[m->head] = (float) v;
  m->head = (m->head + 1) % m->cap;
  if (m->len < m->cap)
    m->len++;
}

double
metric_at (const struct metric *m, size_t i)
{
  if (i >= m->len)
    return NAN;
  return m->buf[(m->head + m->cap - m->len + i) % m->cap];
}

double
metric_last (const struct metric *m)
{
  return m->len ? metric_at (m, m->len - 1) : NAN;
}

bool
metric_stats (const struct metric *m, double *min, double *avg, double *max)
{
  double lo = INFINITY, hi = -INFINITY, sum = 0;
  size_t n = 0;

  for (size_t i = 0; i < m->len; i++)
    {
      double v = metric_at (m, i);
      if (isnan (v))
        continue;
      if (v < lo)
        lo = v;
      if (v > hi)
        hi = v;
      sum += v;
      n++;
    }
  if (n == 0)
    return false;
  *min = lo;
  *avg = sum / n;
  *max = hi;
  return true;
}

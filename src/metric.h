/* metric.h -- fixed-size history of one measured value.
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

#ifndef DIALS_METRIC_H
#define DIALS_METRIC_H

#include <stdbool.h>
#include <stddef.h>

/* A ring buffer of samples.  NaN marks a sample that could not be
   taken.  Pushing never allocates.  */
struct metric
{
  float *buf;
  size_t cap;			/* Capacity.  */
  size_t len;			/* Number of samples held.  */
  size_t head;			/* Where the next sample goes.  */
};

void metric_init (struct metric *m, size_t cap);
void metric_free (struct metric *m);
void metric_clear (struct metric *m);
void metric_push (struct metric *m, double v);

/* Return sample I, counting from the oldest one held.  */
double metric_at (const struct metric *m, size_t i);

/* Return the newest sample, or NaN if there is none.  */
double metric_last (const struct metric *m);

/* Store the lowest, mean and highest of the samples held.  Return
   false if there are none.  */
bool metric_stats (const struct metric *m, double *min, double *avg,
                   double *max);

#endif /* DIALS_METRIC_H */

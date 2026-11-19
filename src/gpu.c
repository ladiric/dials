/* gpu.c -- graphics processors and their history.
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

#include "gpu.h"

void
gpu_sample_clear (struct gpu_sample *s)
{
  s->temp = NAN;
  s->util = s->mem_util = s->enc = s->dec = s->fan = NAN;
  s->mem_used = s->mem_total = NAN;
  s->power = s->power_limit = NAN;
  s->clock_sm = s->clock_sm_max = s->clock_mem = s->clock_mem_max = NAN;
  s->pstate = -1;
  s->nproc = 0;
}

void
gpus_open (struct gpus *g, bool demo, size_t history)
{
  memset (g, 0, sizeof *g);
  const struct gpu_backend *be = demo ? &gpu_demo_backend : &gpu_nvml_backend;
  if (!be->open (&g->priv, g->driver, sizeof g->driver,
                 g->error, sizeof g->error))
    return;

  int n = be->count (g->priv);
  if (n <= 0)
    {
      snprintf (g->error, sizeof g->error, "%s", _("no NVIDIA GPU found"));
      be->close (g->priv);
      g->priv = NULL;
      return;
    }
  if (n > GPU_MAX)
    n = GPU_MAX;

  g->be = be;
  g->count = n;
  for (int i = 0; i < n; i++)
    {
      struct gpu_dev *d = &g->dev[i];
      be->name (g->priv, i, d->name, sizeof d->name);
      gpu_sample_clear (&d->cur);
      metric_init (&d->temp, history);
      metric_init (&d->util, history);
      metric_init (&d->mem, history);
      metric_init (&d->power, history);
    }
}

void
gpus_sample (struct gpus *g, bool procs)
{
  for (int i = 0; i < g->count; i++)
    {
      struct gpu_dev *d = &g->dev[i];
      gpu_sample_clear (&d->cur);
      g->be->sample (g->priv, i, procs, &d->cur);
      metric_push (&d->temp, d->cur.temp);
      metric_push (&d->util, d->cur.util);
      metric_push (&d->mem, d->cur.mem_used);
      metric_push (&d->power, d->cur.power);
    }
  g->procs_valid = procs;
}

void
gpus_clear_history (struct gpus *g)
{
  for (int i = 0; i < g->count; i++)
    {
      metric_clear (&g->dev[i].temp);
      metric_clear (&g->dev[i].util);
      metric_clear (&g->dev[i].mem);
      metric_clear (&g->dev[i].power);
    }
}

void
gpus_close (struct gpus *g)
{
  for (int i = 0; i < g->count; i++)
    {
      metric_free (&g->dev[i].temp);
      metric_free (&g->dev[i].util);
      metric_free (&g->dev[i].mem);
      metric_free (&g->dev[i].power);
    }
  if (g->be)
    g->be->close (g->priv);
  g->be = NULL;
  g->priv = NULL;
  g->count = 0;
}

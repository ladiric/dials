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
#include "util.h"

void
gpu_sample_clear (struct gpu_sample *s)
{
  s->temp = NAN;
  s->util = s->mem_util = s->enc = s->dec = s->fan = NAN;
  s->mem_used = s->mem_total = NAN;
  s->power = s->power_limit = NAN;
  s->clock_sm = s->clock_sm_max = s->clock_mem = s->clock_mem_max = NAN;
  s->pstate = -1;
  s->procs_known = false;
  s->nproc = 0;
}

void
gpus_open (struct gpus *g, bool demo, size_t history)
{
  memset (g, 0, sizeof *g);

  /* A copy of /sys says nothing to NVML, so only the kernel's drivers
     are read from it.  */
  const struct gpu_backend *wanted[GPU_BACKENDS];
  int nwanted = 0;
  if (demo)
    wanted[nwanted++] = &gpu_demo_backend;
  else
    {
      if (!sysroot_set ())
        wanted[nwanted++] = &gpu_nvml_backend;
      wanted[nwanted++] = &gpu_drm_backend;
    }

  for (int b = 0; b < nwanted; b++)
    {
      char err[160] = "";
      void *priv;
      if (!wanted[b]->open (&priv, err, sizeof err))
        {
          /* Keep the first reason, which is NVML's.  */
          if (!g->error[0])
            snprintf (g->error, sizeof g->error, "%s", err);
          continue;
        }
      int n = wanted[b]->count (priv);
      if (n <= 0)
        {
          wanted[b]->close (priv);
          continue;
        }
      int k = g->nbackends++;
      g->backends[k].be = wanted[b];
      g->backends[k].priv = priv;
      for (int i = 0; i < n && g->count < GPU_MAX; i++)
        {
          struct gpu_dev *d = &g->dev[g->count++];
          d->backend = k;
          d->index = i;
          wanted[b]->describe (priv, i, d->name, sizeof d->name,
                               d->driver, sizeof d->driver);
          gpu_sample_clear (&d->cur);
          metric_init (&d->temp, history);
          metric_init (&d->util, history);
          metric_init (&d->mem, history);
          metric_init (&d->power, history);
        }
    }
  if (g->count == 0 && !g->error[0])
    snprintf (g->error, sizeof g->error, "%s", _("no GPU found"));

  /* Figures worked out from counters need a first reading.  */
  gpus_sample (g, false);
  gpus_clear_history (g);
}

void
gpus_sample (struct gpus *g, bool details)
{
  for (int i = 0; i < g->count; i++)
    {
      struct gpu_dev *d = &g->dev[i];
      gpu_sample_clear (&d->cur);
      g->backends[d->backend].be->sample (g->backends[d->backend].priv,
                                          d->index, details, &d->cur);
      metric_push (&d->temp, d->cur.temp);
      metric_push (&d->util, d->cur.util);
      metric_push (&d->mem, d->cur.mem_used);
      metric_push (&d->power, d->cur.power);
    }
  g->procs_valid = details;
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
  for (int b = 0; b < g->nbackends; b++)
    g->backends[b].be->close (g->backends[b].priv);
  g->nbackends = 0;
  g->count = 0;
}

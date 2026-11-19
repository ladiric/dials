/* gpu_demo.c -- simulated GPUs for --demo.
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

#include <stdint.h>
#include <stdio.h>

#include "gpu.h"
#include "util.h"

/* Two GPUs doing a random walk: utilization jumps now and then,
   temperature and power follow it, memory drifts.  */

struct demo_gpu
{
  const char *name;
  double mem_total, power_limit;
  double temp, util, mem;
};

struct demo_gpus
{
  uint64_t rng;
  struct demo_gpu g[2];
};

/* Processes on each GPU, with their share of its work and memory.  */
static const struct
{
  unsigned int pid;
  const char *name;
  bool compute, graphics;
  double share;
} demo_procs[] = {
  { 4242, "python3", true, false, 0.70 },
  { 1337, "blender", true, true, 0.25 },
  { 2001, "Xorg", false, true, 0.05 },
};

static double
rnd (struct demo_gpus *p)
{
  p->rng ^= p->rng << 13;
  p->rng ^= p->rng >> 7;
  p->rng ^= p->rng << 17;
  return (double) (p->rng >> 11) / 9007199254740992.0;
}

static double
clamp (double v, double lo, double hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

static bool
demo_open (void **priv, char *err, size_t esize)
{
  (void) err;
  (void) esize;
  struct demo_gpus *p = xcalloc (1, sizeof *p);
  p->rng = UINT64_C (0x2545F4914F6CDD1D);
  p->g[0] = (struct demo_gpu) { "Demo GPU 24 GB", 24576, 450, 45, 30, 3000 };
  p->g[1] = (struct demo_gpu) { "Demo GPU 40 GB", 40960, 400, 40, 60, 12000 };
  *priv = p;
  return true;
}

static int
demo_count (void *priv)
{
  (void) priv;
  return 2;
}

static void
demo_describe (void *priv, int i, char *buf, size_t size, char *driver,
               size_t dsize)
{
  struct demo_gpus *p = priv;
  snprintf (buf, size, "%s", p->g[i].name);
  snprintf (driver, dsize, "%s", "demo");
}

static void
demo_sample (void *priv, int i, bool details, struct gpu_sample *s)
{
  static const double jumps[] = { 5, 20, 55, 85, 99 };
  struct demo_gpus *p = priv;
  struct demo_gpu *g = &p->g[i];

  if (rnd (p) < 0.08)
    g->util = jumps[(int) (rnd (p) * ARRAY_CARDINALITY (jumps))];
  g->util = clamp (g->util + rnd (p) * 24 - 12, 0, 100);
  g->temp += (35 + 0.55 * g->util - g->temp) * 0.15 + rnd (p) * 1.2 - 0.6;
  g->mem = clamp (g->mem + rnd (p) * 1900 - 900, 500, g->mem_total);

  s->temp = g->temp;
  s->util = g->util;
  s->mem_util = g->util * 0.6;
  s->mem_used = g->mem;
  s->mem_total = g->mem_total;
  s->power = clamp (40 + (g->power_limit - 40) * g->util / 100
                    + rnd (p) * 16 - 8, 0, g->power_limit);
  s->power_limit = g->power_limit;
  s->clock_sm = 300 + 2220 * g->util / 100;
  s->clock_sm_max = 2520;
  s->clock_mem = g->util > 5 ? 10501 : 405;
  s->clock_mem_max = 10501;
  s->fan = clamp (30 + (g->temp - 40) * 1.5, 30, 100);
  s->enc = g->util > 50 ? rnd (p) * 20 : 0;
  s->dec = 0;
  s->pstate = g->util > 5 ? 2 : 8;

  s->procs_known = true;
  if (details)
    for (size_t k = 0; k < ARRAY_CARDINALITY (demo_procs); k++)
      {
        struct gpu_proc *gp = &s->proc[s->nproc++];
        gp->pid = demo_procs[k].pid + (unsigned int) i;
        gp->compute = demo_procs[k].compute;
        gp->graphics = demo_procs[k].graphics;
        gp->mem = g->mem * demo_procs[k].share;
        gp->sm = g->util * demo_procs[k].share;
        snprintf (gp->name, sizeof gp->name, "%s", demo_procs[k].name);
      }
}

static void
demo_close (void *priv)
{
  free (priv);
}

const struct gpu_backend gpu_demo_backend = {
  demo_open, demo_count, demo_describe, demo_sample, demo_close
};

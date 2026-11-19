/* source_demo.c -- simulated measurements for --demo.
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
#include <time.h>

#include "source.h"
#include "util.h"

/* A random walk that looks like a busy 16-core machine.  It is useful
   for screenshots, for tests and for working on the interface without
   the real hardware.  */

enum { DEMO_MEM = 32768, DEMO_SWAP = 8192 };

struct demo_src
{
  uint64_t rng;
  double cpu, mem, swap, load[3];
  time_t start;
};

/* Return a pseudo-random number in [0, 1).  */
static double
rnd (struct demo_src *p)
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
demo_open (struct source *s)
{
  struct demo_src *p = xcalloc (1, sizeof *p);
  p->rng = UINT64_C (0x9E3779B97F4A7C15);
  p->cpu = 30;
  p->mem = 12000;
  p->swap = 300;
  p->start = time (NULL);
  s->priv = p;
  return true;
}

static void
demo_info (struct source *s, struct sys_info *info)
{
  (void) s;
  str_copy (info->host, sizeof info->host, "demo", 4);
  snprintf (info->os, sizeof info->os, "%s", _("Demo System 1.0"));
  str_copy (info->kernel, sizeof info->kernel, "6.0.0-demo", 10);
  str_copy (info->arch, sizeof info->arch, "x86_64", 6);
  str_copy (info->cpu_model, sizeof info->cpu_model,
            "Demo CPU 16-Core Processor", 26);
  info->ncpu = 16;
}

static void
demo_sample (struct source *s, struct sys_sample *out)
{
  static const double jumps[] = { 5, 20, 55, 85, 99 };
  struct demo_src *p = s->priv;

  if (rnd (p) < 0.08)
    p->cpu = jumps[(int) (rnd (p) * ARRAY_CARDINALITY (jumps))];
  p->cpu = clamp (p->cpu + rnd (p) * 24 - 12, 0, 100);
  p->mem = clamp (p->mem + rnd (p) * 1900 - 900, 2000, DEMO_MEM);
  p->swap = clamp (p->swap + rnd (p) * 60 - 30, 0, 2048);

  double target = p->cpu / 100 * 16;
  static const double weight[3] = { 0.3, 0.08, 0.03 };
  for (int i = 0; i < 3; i++)
    p->load[i] += (target - p->load[i]) * weight[i];

  out->cpu_pct = p->cpu;
  out->mem_used = p->mem;
  out->mem_total = DEMO_MEM;
  out->swap_used = p->swap;
  out->swap_total = DEMO_SWAP;
  memcpy (out->load, p->load, sizeof out->load);
  out->uptime = 3 * 86400 + 4 * 3600 + 12 * 60
                + (double) (time (NULL) - p->start);
}

static void
demo_close (struct source *s)
{
  free (s->priv);
}

const struct source_ops source_demo_ops = {
  demo_open, demo_info, demo_sample, demo_close
};

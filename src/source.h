/* source.h -- where system measurements come from.
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

#ifndef DIALS_SOURCE_H
#define DIALS_SOURCE_H

#include <stdbool.h>

/* Facts that do not change while the program runs.  */
struct sys_info
{
  char host[256];
  char os[128];
  char kernel[128];
  char arch[64];
  char cpu_model[128];
};

/* One sample.  Unknown values are NaN.  */
struct sys_sample
{
  double load[3];
  double uptime;		/* Seconds.  */
};

struct source;

/* Facts about the whole system, from the running system or simulated.
   The processors, memory, disks and GPUs have modules of their own.  */
struct source_ops
{
  bool (*open) (struct source *s);
  void (*info) (struct source *s, struct sys_info *info);
  void (*sample) (struct source *s, struct sys_sample *out);
  void (*close) (struct source *s);
};

struct source
{
  const struct source_ops *ops;
  void *priv;
};

extern const struct source_ops source_linux_ops;
extern const struct source_ops source_demo_ops;

/* Return a new source, or NULL with errno set.  */
struct source *source_new (bool demo);
void source_free (struct source *s);
void source_info (struct source *s, struct sys_info *info);
void source_sample (struct source *s, struct sys_sample *out);
void sys_sample_clear (struct sys_sample *s);

#endif /* DIALS_SOURCE_H */

/* source.c -- where system measurements come from.
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

#include <errno.h>

#include "source.h"
#include "util.h"

struct source *
source_new (bool demo)
{
  struct source *s = xcalloc (1, sizeof *s);
  s->ops = demo ? &source_demo_ops : &source_linux_ops;
  if (!s->ops->open (s))
    {
      int e = errno;
      free (s);
      errno = e;
      return NULL;
    }
  return s;
}

void
source_free (struct source *s)
{
  if (!s)
    return;
  s->ops->close (s);
  free (s);
}

void
source_info (struct source *s, struct sys_info *info)
{
  memset (info, 0, sizeof *info);
  s->ops->info (s, info);
}

void
source_sample (struct source *s, struct sys_sample *out)
{
  sys_sample_clear (out);
  s->ops->sample (s, out);
}

void
sys_sample_clear (struct sys_sample *s)
{
  s->load[0] = s->load[1] = s->load[2] = NAN;
  s->uptime = NAN;
}

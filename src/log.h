/* log.h -- writing measurements to a file.
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

#ifndef DIALS_LOG_H
#define DIALS_LOG_H

#include <stdbool.h>
#include <stdio.h>

#include "disk.h"
#include "net.h"

struct app;

/* A log in CSV, one line per sample with a heading line, or in JSON
   lines, one object per sample, for files named *.json or *.jsonl.
   Its columns are fixed when it starts: disks and interfaces that
   appear later are not logged.  */
struct logger
{
  FILE *f;
  bool json;
  char path[4096];
  int ndisk, nnet;
  char disks[DISK_MAX][32];
  char nets[NET_MAX][32];
};

/* Start logging to PATH, appending if it exists.  Return 0 or an
   errno value.  */
int log_start (struct logger *l, const char *path, const struct app *a);

/* Write the latest sample.  Return 0 or an errno value.  */
int log_write (struct logger *l, const struct app *a);

void log_stop (struct logger *l);

#endif /* DIALS_LOG_H */

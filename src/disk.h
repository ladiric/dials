/* disk.h -- disk activity and file system space.
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

#ifndef DIALS_DISK_H
#define DIALS_DISK_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum
{
  DISK_MAX = 32,
  FS_MAX = 32,
  DISK_COUNTERS = 11		/* Fields of /proc/diskstats used.  */
};

/* A whole disk.  Readings are NaN when unknown.  */
struct disk_dev
{
  char name[32];
  char model[64];
  double size;			/* Bytes.  */
  bool rotational;
  double rd_bps, wr_bps;	/* Bytes per second.  */
  double rd_iops, wr_iops;	/* Operations per second.  */
  double util;			/* Percent of time busy.  */
  double await;			/* Mean milliseconds per operation.  */
  double inflight;		/* Operations in progress.  */
  double temp;			/* Degrees Celsius.  */
  struct metric m_rd, m_wr, m_util;	/* MiB/s, MiB/s, percent.  */

  /* Private.  */
  unsigned long long prev[DISK_COUNTERS];
  bool have_prev;
  int fd_temp;
};

/* A mounted file system.  Sizes are in bytes, NaN when unknown.  */
struct fs_entry
{
  char mount[256];
  char device[128];
  char type[32];
  double size, used, avail;
};

struct disks
{
  int n;
  struct disk_dev dev[DISK_MAX];
  int nfs;
  struct fs_entry fs[FS_MAX];

  /* Private.  */
  bool demo;
  uint64_t rng;
  int fd_stats;
  double prev_time;
  char buf[65536];
};

void disks_open (struct disks *d, bool demo, size_t history);

/* Take a reading; measure the file systems too if FS.  */
void disks_sample (struct disks *d, bool fs);
void disks_clear_history (struct disks *d);
void disks_close (struct disks *d);

#endif /* DIALS_DISK_H */

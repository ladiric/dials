/* memory.h -- how memory and swap are used.
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

#ifndef DIALS_MEMORY_H
#define DIALS_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum { MEM_TOP = 10 };		/* Processes listed.  */

struct mem_proc
{
  unsigned int pid;
  double rss;			/* MiB in memory.  */
  char name[64];
};

/* Readings in MiB, NaN when unknown, and their history.  */
struct memory
{
  double total, free, available, used;
  double apps, kernel, buffers, cached, shared;	/* Parts of TOTAL.  */
  double slab, slab_reclaimable;
  double swap_total, swap_used, swap_cached;
  double dirty, writeback, committed, commit_limit;
  double huge_total, huge_free;	/* Pages.  */
  double huge_size;		/* MiB per huge page.  */
  double zram_orig, zram_compr, zram_used;
  double faults, major_faults, swap_in, swap_out;	/* Pages per second.  */
  double psi_some, psi_full;	/* Percent of time stalled.  */
  bool top_valid;		/* Whether TOP was read this time.  */
  int ntop;
  struct mem_proc top[MEM_TOP];
  struct metric m_used, m_swap;	/* Percent of total.  */

  /* Private.  */
  bool demo;
  uint64_t rng;
  int fd_meminfo, fd_vmstat, fd_psi;
  unsigned long long prev[4];
  double prev_time;
  char buf[16384];
};

void memory_open (struct memory *m, bool demo, size_t history);

/* Take a reading; list the largest processes too if TOP.  */
void memory_sample (struct memory *m, bool top);
void memory_clear_history (struct memory *m);
void memory_close (struct memory *m);

#endif /* DIALS_MEMORY_H */

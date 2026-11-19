/* cpu.h -- processor use, frequency and temperature.
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

#ifndef DIALS_CPU_H
#define DIALS_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum
{
  CPU_MAX = 1024,		/* Processors shown.  */
  CPU_FIELDS = 8		/* Time counters per processor used.  */
};

/* Where the processors' time went, in percent.  */
struct cpu_split
{
  double user, nice, system, iowait, irq, steal;
};

struct cpu_core
{
  double pct;			/* Percent busy, or NaN if offline.  */
  double mhz;
};

/* Readings, NaN when unknown, and their history.  */
struct cpu
{
  int n;			/* Logical processors.  */
  double total;			/* Percent busy.  */
  struct cpu_split split;
  struct cpu_core *core;	/* N entries.  */
  double mhz_avg, mhz_top;	/* Mean and highest current frequency.  */
  double mhz_max;		/* Highest frequency the hardware allows.  */
  double temp;			/* Package temperature, degrees Celsius.  */
  double psi;			/* Percent of time tasks waited for a CPU.  */
  double ctxt, intr, forks;	/* Per second.  */
  double running, blocked;	/* Tasks.  */
  struct metric m_total, m_temp, m_mhz;

  /* Private.  */
  bool demo;
  uint64_t rng;
  int fd_stat, fd_psi, fd_temp;
  int *fd_freq;
  unsigned long long (*prev)[CPU_FIELDS];	/* N + 1 rows; 0 is all.  */
  bool *have_prev;
  unsigned long long prev_ctxt, prev_intr, prev_forks;
  double prev_time;
  char *buf;
  size_t bufsize;
};

void cpu_open (struct cpu *c, bool demo, size_t history);
void cpu_sample (struct cpu *c);
void cpu_clear_history (struct cpu *c);
void cpu_close (struct cpu *c);

#endif /* DIALS_CPU_H */

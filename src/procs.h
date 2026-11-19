/* procs.h -- the processes running on the system.
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

#ifndef DIALS_PROCS_H
#define DIALS_PROCS_H

#include <stdbool.h>
#include <stdint.h>

struct proc_info
{
  unsigned int pid, ppid, uid;
  char state;			/* R running, S sleeping, D waiting, ...  */
  int nice;
  int threads;
  double cpu;			/* Percent of one processor; NaN at first.  */
  double rss;			/* MiB in memory.  */
  double time;			/* Seconds of processor time used.  */
  char user[32];
  char comm[32];		/* Short name.  */
  char cmd[256];		/* Command line, or "[comm]" for a kernel
                                   thread.  */

  /* Private.  */
  unsigned long long ticks, start;
};

struct procs
{
  int n;
  struct proc_info *p;		/* By PID.  */
  int running, sleeping, waiting, threads;
  bool valid;			/* Whether the list has been read.  */

  /* Private.  */
  int cap;
  struct proc_info *prev;
  int nprev, prevcap;
  bool demo;
  uint64_t rng;
  double prev_time;
  long hz;
  double page_mib;
  int nusers;
  struct
  {
    unsigned int uid;
    char name[32];
  } users[64];
};

void procs_open (struct procs *s, bool demo);
void procs_sample (struct procs *s);
void procs_close (struct procs *s);

/* Return the process with PID, or NULL.  */
const struct proc_info *procs_find (const struct procs *s, unsigned int pid) ATTRIBUTE_PURE;

/* Send signal SIG to process PID, or change its nice value to NICE.
   Return 0 or an errno value; with simulated data, do nothing and
   return -1.  */
int procs_signal (struct procs *s, unsigned int pid, int sig);
int procs_renice (struct procs *s, unsigned int pid, int nice);

#endif /* DIALS_PROCS_H */

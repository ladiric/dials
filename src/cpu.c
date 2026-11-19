/* cpu.c -- processor use, frequency and temperature.
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

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

#include "cpu.h"
#include "util.h"

enum { DEMO_CPUS = 16 };

/* Hardware monitoring drivers that report the processor package
   temperature, best first.  */
static const char *const temp_drivers[] = {
  "coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal", "acpitz"
};

static double
rnd (struct cpu *c)
{
  c->rng ^= c->rng << 13;
  c->rng ^= c->rng >> 7;
  c->rng ^= c->rng << 17;
  return (double) (c->rng >> 11) / 9007199254740992.0;
}

static double
clamp (double v, double lo, double hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

/* Return the number of the processor on the "cpuN" line at S, or -1 if
   S is not such a line.  */
static int
cpu_line (const char *s)
{
  if (strncmp (s, "cpu", 3) != 0 || s[3] < '0' || s[3] > '9')
    return -1;
  return atoi (s + 3);
}

/* Open the temp1_input of the first hwmon device whose driver is one of
   temp_drivers.  */
static int
open_temp (void)
{
  char path[4096];
  sys_path (path, sizeof path, "/sys/class/hwmon");
  DIR *dir = opendir (path);
  if (!dir)
    return -1;

  int best = ARRAY_CARDINALITY (temp_drivers), fd = -1;
  struct dirent *e;
  while ((e = readdir (dir)))
    {
      char rel[512], name[64];
      if (strncmp (e->d_name, "hwmon", 5) != 0)
        continue;
      snprintf (rel, sizeof rel, "/sys/class/hwmon/%s/name", e->d_name);
      if (!sys_read (rel, name, sizeof name))
        continue;
      name[strcspn (name, "\n")] = '\0';
      for (int i = 0; i < best; i++)
        if (strcmp (name, temp_drivers[i]) == 0)
          {
            snprintf (rel, sizeof rel, "/sys/class/hwmon/%s/temp1_input",
                      e->d_name);
            int f = sys_open (rel);
            if (f >= 0)
              {
                if (fd >= 0)
                  close (fd);
                fd = f;
                best = i;
              }
            break;
          }
    }
  closedir (dir);
  return fd;
}

void
cpu_open (struct cpu *c, bool demo, size_t history)
{
  memset (c, 0, sizeof *c);
  c->demo = demo;
  c->fd_stat = c->fd_psi = c->fd_temp = -1;
  c->bufsize = 256 * 1024;
  c->buf = xmalloc (c->bufsize);
  c->rng = UINT64_C (0x8C2D1E9B3F4A5701);

  if (demo)
    c->n = DEMO_CPUS;
  else
    {
      c->fd_stat = sys_open ("/proc/stat");
      if (read_at0 (c->fd_stat, c->buf, c->bufsize) > 0)
        for (const char *line = c->buf; line; line = strchr (line, '\n'))
          {
            if (*line == '\n')
              line++;
            int k = cpu_line (line);
            if (k >= 0 && k < CPU_MAX && k + 1 > c->n)
              c->n = k + 1;
          }
      c->fd_psi = sys_open ("/proc/pressure/cpu");
      c->fd_temp = open_temp ();
    }
  if (c->n == 0)
    c->n = 1;

  c->core = xcalloc (c->n, sizeof *c->core);
  c->fd_freq = xmalloc (c->n * sizeof *c->fd_freq);
  c->prev = xcalloc (c->n + 1, sizeof *c->prev);
  c->have_prev = xcalloc (c->n + 1, sizeof *c->have_prev);
  for (int i = 0; i < c->n; i++)
    {
      char rel[128];
      snprintf (rel, sizeof rel,
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
      c->fd_freq[i] = demo ? -1 : sys_open (rel);
    }

  double khz;
  c->mhz_max = NAN;
  if (demo)
    c->mhz_max = 5000;
  else if (sys_number ("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
                       &khz))
    c->mhz_max = khz / 1000;

  metric_init (&c->m_total, history);
  metric_init (&c->m_temp, history);
  metric_init (&c->m_mhz, history);
  c->prev_time = now_mono ();
  cpu_sample (c);		/* Counters need a first reading.  */
  cpu_clear_history (c);
}

static void
clear_readings (struct cpu *c)
{
  c->total = NAN;
  c->split = (struct cpu_split) { NAN, NAN, NAN, NAN, NAN, NAN };
  for (int i = 0; i < c->n; i++)
    c->core[i].pct = c->core[i].mhz = NAN;
  c->mhz_avg = c->mhz_top = c->temp = c->psi = NAN;
  c->ctxt = c->intr = c->forks = c->running = c->blocked = NAN;
}

/* Turn the counters V of row ROW into percentages.  */
static void
use_counters (struct cpu *c, int row, const unsigned long long *v)
{
  unsigned long long *p = c->prev[row];
  if (c->have_prev[row])
    {
      double d[CPU_FIELDS], dt = 0;
      for (int k = 0; k < CPU_FIELDS; k++)
        {
          /* Counters can step back a little; treat that as no time.  */
          d[k] = v[k] >= p[k] ? (double) (v[k] - p[k]) : 0;
          dt += d[k];
        }
      if (dt > 0)
        {
          double busy = 100 * (1 - (d[3] + d[4]) / dt);
          if (row == 0)
            {
              c->total = clamp (busy, 0, 100);
              c->split.user = 100 * d[0] / dt;
              c->split.nice = 100 * d[1] / dt;
              c->split.system = 100 * d[2] / dt;
              c->split.iowait = 100 * d[4] / dt;
              c->split.irq = 100 * (d[5] + d[6]) / dt;
              c->split.steal = 100 * d[7] / dt;
            }
          else
            c->core[row - 1].pct = clamp (busy, 0, 100);
        }
    }
  memcpy (p, v, sizeof c->prev[row]);
  c->have_prev[row] = true;
}

static double
rate (unsigned long long now, unsigned long long *prev, double dt)
{
  double r = *prev && now >= *prev && dt > 0 ? (now - *prev) / dt : NAN;
  *prev = now;
  return r;
}

static void
linux_sample (struct cpu *c, double dt)
{
  if (read_at0 (c->fd_stat, c->buf, c->bufsize) > 0)
    for (char *line = c->buf; line && *line;)
      {
        char *next = strchr (line, '\n');
        if (next)
          *next++ = '\0';
        int k = cpu_line (line);
        bool all = strncmp (line, "cpu ", 4) == 0;
        if (all || (k >= 0 && k < c->n))
          {
            unsigned long long v[CPU_FIELDS] = { 0 };
            char *s = line + (all ? 3 : 4), *end;
            if (!all)
              s = strchr (line, ' ');
            for (int i = 0; s && i < CPU_FIELDS; i++, s = end)
              {
                v[i] = strtoull (s, &end, 10);
                if (end == s)
                  break;
              }
            use_counters (c, all ? 0 : k + 1, v);
          }
        else if (strncmp (line, "ctxt ", 5) == 0)
          c->ctxt = rate (strtoull (line + 5, NULL, 10), &c->prev_ctxt, dt);
        else if (strncmp (line, "intr ", 5) == 0)
          c->intr = rate (strtoull (line + 5, NULL, 10), &c->prev_intr, dt);
        else if (strncmp (line, "processes ", 10) == 0)
          c->forks = rate (strtoull (line + 10, NULL, 10), &c->prev_forks, dt);
        else if (strncmp (line, "procs_running ", 14) == 0)
          c->running = c_strtod (line + 14, NULL);
        else if (strncmp (line, "procs_blocked ", 14) == 0)
          c->blocked = c_strtod (line + 14, NULL);
        line = next;
      }

  char buf[512];
  if (read_at0 (c->fd_psi, buf, sizeof buf) > 0)
    c->psi = psi_avg10 (buf, "some");
  if (read_at0 (c->fd_temp, buf, sizeof buf) > 0)
    c->temp = c_strtod (buf, NULL) / 1000;
  for (int i = 0; i < c->n; i++)
    if (read_at0 (c->fd_freq[i], buf, sizeof buf) > 0)
      c->core[i].mhz = c_strtod (buf, NULL) / 1000;
}

static void
demo_sample (struct cpu *c)
{
  static const double jumps[] = { 5, 15, 40, 70, 95 };
  double base = isnan (metric_last (&c->m_total)) ? 30 : metric_last (&c->m_total);
  if (rnd (c) < 0.08)
    base = jumps[(int) (rnd (c) * ARRAY_CARDINALITY (jumps))];
  base = clamp (base + rnd (c) * 16 - 8, 1, 100);

  double sum = 0;
  for (int i = 0; i < c->n; i++)
    {
      c->core[i].pct = clamp (base + rnd (c) * 40 - 20, 0, 100);
      c->core[i].mhz = 800 + 4200 * c->core[i].pct / 100;
      sum += c->core[i].pct;
    }
  c->total = sum / c->n;
  c->split = (struct cpu_split) {
    c->total * 0.72, c->total * 0.03, c->total * 0.18, c->total * 0.04,
    c->total * 0.03, 0
  };
  c->temp = 38 + c->total * 0.45 + rnd (c) * 2;
  c->psi = c->total > 80 ? (c->total - 80) / 2 : 0;
  c->ctxt = 20000 + c->total * 900;
  c->intr = 12000 + c->total * 400;
  c->forks = 5 + rnd (c) * 20;
  c->running = 1 + c->n * c->total / 100;
  c->blocked = 0;
}

void
cpu_sample (struct cpu *c)
{
  double now = now_mono (), dt = now - c->prev_time;
  c->prev_time = now;

  clear_readings (c);
  if (c->demo)
    demo_sample (c);
  else
    linux_sample (c, dt);

  double sum = 0;
  int known = 0;
  for (int i = 0; i < c->n; i++)
    if (!isnan (c->core[i].mhz))
      {
        sum += c->core[i].mhz;
        known++;
        if (isnan (c->mhz_top) || c->core[i].mhz > c->mhz_top)
          c->mhz_top = c->core[i].mhz;
      }
  if (known)
    c->mhz_avg = sum / known;

  metric_push (&c->m_total, c->total);
  metric_push (&c->m_temp, c->temp);
  metric_push (&c->m_mhz, c->mhz_avg);
}

void
cpu_clear_history (struct cpu *c)
{
  metric_clear (&c->m_total);
  metric_clear (&c->m_temp);
  metric_clear (&c->m_mhz);
}

void
cpu_close (struct cpu *c)
{
  int fds[] = { c->fd_stat, c->fd_psi, c->fd_temp };
  for (size_t i = 0; i < ARRAY_CARDINALITY (fds); i++)
    if (fds[i] >= 0)
      close (fds[i]);
  for (int i = 0; i < c->n; i++)
    if (c->fd_freq[i] >= 0)
      close (c->fd_freq[i]);
  metric_free (&c->m_total);
  metric_free (&c->m_temp);
  metric_free (&c->m_mhz);
  free (c->fd_freq);
  free (c->prev);
  free (c->have_prev);
  free (c->core);
  free (c->buf);
}

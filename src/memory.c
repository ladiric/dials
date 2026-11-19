/* memory.c -- how memory and swap are used.
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

#include "memory.h"
#include "util.h"

enum { DEMO_TOTAL = 32768, DEMO_SWAP = 8192 };

/* The /proc/vmstat counters turned into rates.  */
static const char *const vm_keys[4] = {
  "pgfault", "pgmajfault", "pswpin", "pswpout"
};

static double
rnd (struct memory *m)
{
  m->rng ^= m->rng << 13;
  m->rng ^= m->rng >> 7;
  m->rng ^= m->rng << 17;
  return (double) (m->rng >> 11) / 9007199254740992.0;
}

static double
clamp (double v, double lo, double hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

/* Return the value of the line "KEY: value" or "KEY value" of BUF,
   or NaN.  */
static double
field (const char *buf, const char *key)
{
  size_t len = strlen (key);
  for (const char *line = buf; line && *line;)
    {
      if (strncmp (line, key, len) == 0
          && (line[len] == ':' || line[len] == ' '))
        return c_strtod (line + len + 1, NULL);
      line = strchr (line, '\n');
      if (line)
        line++;
    }
  return NAN;
}

void
memory_open (struct memory *m, bool demo, size_t history)
{
  memset (m, 0, sizeof *m);
  m->demo = demo;
  m->rng = UINT64_C (0x5DEECE66D1234567);
  m->fd_meminfo = m->fd_vmstat = m->fd_psi = -1;
  if (!demo)
    {
      m->fd_meminfo = sys_open ("/proc/meminfo");
      m->fd_vmstat = sys_open ("/proc/vmstat");
      m->fd_psi = sys_open ("/proc/pressure/memory");
    }
  metric_init (&m->m_used, history);
  metric_init (&m->m_swap, history);
  m->prev_time = now_mono ();
  memory_sample (m, false);	/* Rates need a first reading.  */
  memory_clear_history (m);
}

static void
clear_readings (struct memory *m)
{
  double *all[] = {
    &m->total, &m->free, &m->available, &m->used, &m->apps, &m->kernel,
    &m->buffers, &m->cached, &m->shared, &m->slab, &m->slab_reclaimable,
    &m->swap_total, &m->swap_used, &m->swap_cached, &m->dirty,
    &m->writeback, &m->committed, &m->commit_limit, &m->huge_total,
    &m->huge_free, &m->huge_size, &m->zram_orig, &m->zram_compr,
    &m->zram_used, &m->faults, &m->major_faults, &m->swap_in,
    &m->swap_out, &m->psi_some, &m->psi_full
  };
  for (size_t i = 0; i < ARRAY_CARDINALITY (all); i++)
    *all[i] = NAN;
  m->top_valid = false;
  m->ntop = 0;
}

/* Add up the zram devices' original and compressed data and the
   memory they use, from their mm_stat files.  */
static void
read_zram (struct memory *m)
{
  char path[4096];
  sys_path (path, sizeof path, "/sys/block");
  DIR *dir = opendir (path);
  if (!dir)
    return;
  struct dirent *e;
  while ((e = readdir (dir)))
    {
      char rel[512], buf[512];
      if (strncmp (e->d_name, "zram", 4) != 0)
        continue;
      snprintf (rel, sizeof rel, "/sys/block/%s/mm_stat", e->d_name);
      if (!sys_read (rel, buf, sizeof buf))
        continue;
      double v[3];
      char *s = buf, *end;
      int k;
      for (k = 0; k < 3; k++, s = end)
        {
          v[k] = c_strtod (s, &end);
          if (end == s)
            break;
        }
      if (k < 3)
        continue;
      if (isnan (m->zram_orig))
        m->zram_orig = m->zram_compr = m->zram_used = 0;
      m->zram_orig += v[0] / 1048576;
      m->zram_compr += v[1] / 1048576;
      m->zram_used += v[2] / 1048576;
    }
  closedir (dir);
}

/* Keep in M->top the MEM_TOP processes with the most memory.  */
static void
read_top (struct memory *m)
{
  char path[4096];
  sys_path (path, sizeof path, "/proc");
  DIR *dir = opendir (path);
  if (!dir)
    return;
  double page_mib = sysconf (_SC_PAGESIZE) / 1048576.0;
  struct dirent *e;
  while ((e = readdir (dir)))
    {
      if (e->d_name[0] < '1' || e->d_name[0] > '9')
        continue;
      char rel[512], buf[256];
      snprintf (rel, sizeof rel, "/proc/%s/statm", e->d_name);
      if (!sys_read (rel, buf, sizeof buf))
        continue;
      char *s = strchr (buf, ' ');
      if (!s)
        continue;
      double rss = c_strtod (s, NULL) * page_mib;

      /* Insert in order of size.  */
      int at = m->ntop;
      while (at > 0 && m->top[at - 1].rss < rss)
        at--;
      if (at >= MEM_TOP)
        continue;
      int last = m->ntop < MEM_TOP ? m->ntop : MEM_TOP - 1;
      memmove (&m->top[at + 1], &m->top[at], (last - at) * sizeof m->top[0]);
      m->top[at].pid = (unsigned int) atoi (e->d_name);
      m->top[at].rss = rss;
      m->top[at].name[0] = '\0';
      if (m->ntop < MEM_TOP)
        m->ntop++;
    }
  closedir (dir);

  /* Names only for the processes kept.  */
  for (int i = 0; i < m->ntop; i++)
    {
      char rel[64];
      snprintf (rel, sizeof rel, "/proc/%u/comm", m->top[i].pid);
      if (sys_read (rel, m->top[i].name, sizeof m->top[i].name))
        m->top[i].name[strcspn (m->top[i].name, "\n")] = '\0';
      else
        snprintf (m->top[i].name, sizeof m->top[i].name, "%s", "?");
    }
  m->top_valid = true;
}

static void
linux_sample (struct memory *m, bool top, double dt)
{
  if (read_at0 (m->fd_meminfo, m->buf, sizeof m->buf) > 0)
    {
      const char *b = m->buf;
      double kib = 1.0 / 1024;
      m->total = field (b, "MemTotal") * kib;
      m->free = field (b, "MemFree") * kib;
      m->available = field (b, "MemAvailable") * kib;
      m->buffers = field (b, "Buffers") * kib;
      m->cached = field (b, "Cached") * kib;
      m->shared = field (b, "Shmem") * kib;
      m->slab = field (b, "Slab") * kib;
      m->slab_reclaimable = field (b, "SReclaimable") * kib;
      m->kernel = m->slab + (field (b, "KernelStack")
                             + field (b, "PageTables")) * kib;
      m->swap_total = field (b, "SwapTotal") * kib;
      m->swap_used = m->swap_total - field (b, "SwapFree") * kib;
      m->swap_cached = field (b, "SwapCached") * kib;
      m->dirty = field (b, "Dirty") * kib;
      m->writeback = field (b, "Writeback") * kib;
      m->committed = field (b, "Committed_AS") * kib;
      m->commit_limit = field (b, "CommitLimit") * kib;
      m->huge_total = field (b, "HugePages_Total");
      m->huge_free = field (b, "HugePages_Free");
      m->huge_size = field (b, "Hugepagesize") * kib;
      if (isnan (m->available))
        m->available = m->free + m->buffers + m->cached;
    }

  if (read_at0 (m->fd_vmstat, m->buf, sizeof m->buf) > 0)
    {
      double *out[4] = { &m->faults, &m->major_faults, &m->swap_in,
                         &m->swap_out };
      for (int i = 0; i < 4; i++)
        {
          double v = field (m->buf, vm_keys[i]);
          if (isnan (v))
            continue;
          unsigned long long now = (unsigned long long) v;
          if (m->prev[i] && now >= m->prev[i] && dt > 0)
            *out[i] = (now - m->prev[i]) / dt;
          m->prev[i] = now;
        }
    }

  char buf[512];
  if (read_at0 (m->fd_psi, buf, sizeof buf) > 0)
    {
      m->psi_some = psi_avg10 (buf, "some");
      m->psi_full = psi_avg10 (buf, "full");
    }
  read_zram (m);
  if (top)
    read_top (m);
}

static void
demo_sample (struct memory *m, bool top)
{
  static const struct
  {
    unsigned int pid;
    const char *name;
    double share;
  } procs[] = {
    { 4242, "python3", 0.30 }, { 2210, "firefox", 0.18 },
    { 1337, "blender", 0.12 }, { 3001, "postgres", 0.08 },
    { 1500, "emacs", 0.03 }, { 1200, "gnome-shell", 0.03 },
  };
  double apps = isnan (metric_last (&m->m_used))
    ? 9000 : metric_last (&m->m_used) / 100 * DEMO_TOTAL - 2500;
  apps = clamp (apps + rnd (m) * 1400 - 650, 3000, DEMO_TOTAL - 4000);

  m->total = DEMO_TOTAL;
  m->apps = apps;
  m->kernel = 1500;
  m->slab = 1100;
  m->slab_reclaimable = 700;
  m->buffers = 400;
  m->cached = 9000;
  m->shared = 600;
  m->free = clamp (DEMO_TOTAL - apps - m->kernel - m->buffers - m->cached,
                   0, DEMO_TOTAL);
  m->available = m->free + m->cached + m->buffers + m->slab_reclaimable;
  m->swap_total = DEMO_SWAP;
  m->swap_used = 250 + rnd (m) * 40;
  m->swap_cached = 30;
  m->dirty = rnd (m) * 40;
  m->writeback = 0;
  m->committed = apps * 1.6;
  m->commit_limit = DEMO_TOTAL / 2 + DEMO_SWAP;
  m->huge_total = m->huge_free = 0;
  m->huge_size = 2;
  m->zram_orig = 900;
  m->zram_compr = 260;
  m->zram_used = 280;
  m->faults = 2000 + rnd (m) * 8000;
  m->major_faults = rnd (m) * 3;
  m->swap_in = m->swap_out = 0;
  m->psi_some = apps > DEMO_TOTAL * 0.8 ? 3 : 0;
  m->psi_full = 0;

  if (top)
    {
      for (size_t i = 0; i < ARRAY_CARDINALITY (procs); i++)
        {
          m->top[i].pid = procs[i].pid;
          m->top[i].rss = apps * procs[i].share;
          snprintf (m->top[i].name, sizeof m->top[i].name, "%s",
                    procs[i].name);
        }
      m->ntop = ARRAY_CARDINALITY (procs);
      m->top_valid = true;
    }
}

void
memory_sample (struct memory *m, bool top)
{
  double now = now_mono (), dt = now - m->prev_time;
  m->prev_time = now;

  clear_readings (m);
  if (m->demo)
    demo_sample (m, top);
  else
    linux_sample (m, top, dt);

  m->used = m->total - m->available;
  /* What is left when the kernel, buffers, cache and free memory are
     taken away is in use by programs.  */
  if (!m->demo)
    {
      double apps = m->total - m->free - m->buffers - m->cached - m->kernel;
      m->apps = isnan (apps) ? NAN : clamp (apps, 0, m->total);
    }

  metric_push (&m->m_used, m->total > 0 ? 100 * m->used / m->total : NAN);
  metric_push (&m->m_swap,
               m->swap_total > 0 ? 100 * m->swap_used / m->swap_total : NAN);
}

void
memory_clear_history (struct memory *m)
{
  metric_clear (&m->m_used);
  metric_clear (&m->m_swap);
}

void
memory_close (struct memory *m)
{
  int fds[] = { m->fd_meminfo, m->fd_vmstat, m->fd_psi };
  for (size_t i = 0; i < ARRAY_CARDINALITY (fds); i++)
    if (fds[i] >= 0)
      close (fds[i]);
  metric_free (&m->m_used);
  metric_free (&m->m_swap);
}

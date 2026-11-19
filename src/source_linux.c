/* source_linux.c -- measurements of the running GNU/Linux system.
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

#include <errno.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "source.h"
#include "util.h"

struct linux_src
{
  int fd_stat, fd_meminfo, fd_loadavg, fd_uptime;
  unsigned long long prev_total, prev_idle;
  bool have_prev;
  char buf[16384];
};

/* Read the aggregate CPU line of /proc/stat.  */
static bool
cpu_times (struct linux_src *p, unsigned long long *total,
           unsigned long long *idle)
{
  if (read_at0 (p->fd_stat, p->buf, sizeof p->buf) < 4
      || strncmp (p->buf, "cpu ", 4) != 0)
    return false;

  /* user nice system idle iowait irq softirq steal; the guest fields
     that follow are already counted in user and nice.  */
  unsigned long long v[8] = { 0 };
  char *s = p->buf + 4;
  for (int i = 0; i < 8; i++)
    {
      char *end;
      v[i] = strtoull (s, &end, 10);
      if (end == s)
        break;
      s = end;
    }
  *total = 0;
  for (int i = 0; i < 8; i++)
    *total += v[i];
  *idle = v[3] + v[4];
  return true;
}

/* Return the value of the "KEY: value kB" line in BUF, or NaN.  */
static double
field_kib (const char *buf, const char *key)
{
  size_t len = strlen (key);
  for (const char *line = buf; line && *line;)
    {
      if (strncmp (line, key, len) == 0 && line[len] == ':')
        return c_strtod (line + len + 1, NULL);
      line = strchr (line, '\n');
      if (line)
        line++;
    }
  return NAN;
}

/* Copy into DST the value after KEY on a line that starts with KEY in
   BUF, skipping SKIP characters such as ':', '=' and blanks, and
   dropping surrounding quotes.  Return true if KEY was found.  */
static bool
line_value (const char *buf, const char *key, const char *skip,
            char *dst, size_t size)
{
  size_t len = strlen (key);
  for (const char *line = buf; line && *line;)
    {
      if (strncmp (line, key, len) == 0)
        {
          const char *v = line + len;
          v += strspn (v, skip);
          size_t n = strcspn (v, "\n");
          if (n > 0 && (*v == '"' || *v == '\''))
            {
              v++;
              n--;
              if (n > 0 && (v[n - 1] == '"' || v[n - 1] == '\''))
                n--;
            }
          str_copy (dst, size, v, n);
          return n > 0;
        }
      line = strchr (line, '\n');
      if (line)
        line++;
    }
  return false;
}

static bool
linux_open (struct source *s)
{
  struct linux_src *p = xcalloc (1, sizeof *p);

  p->fd_stat = open_ro ("/proc/stat");
  if (p->fd_stat < 0)
    {
      int e = errno;
      free (p);
      errno = e;
      return false;
    }
  p->fd_meminfo = open_ro ("/proc/meminfo");
  p->fd_loadavg = open_ro ("/proc/loadavg");
  p->fd_uptime = open_ro ("/proc/uptime");
  /* CPU use is a difference, so take the first reading now.  */
  p->have_prev = cpu_times (p, &p->prev_total, &p->prev_idle);
  s->priv = p;
  return true;
}

static void
linux_info (struct source *s, struct sys_info *info)
{
  struct linux_src *p = s->priv;
  struct utsname u;

  if (gethostname (info->host, sizeof info->host - 1) != 0)
    str_copy (info->host, sizeof info->host, "?", 1);
  if (uname (&u) == 0)
    {
      str_copy (info->kernel, sizeof info->kernel, u.release,
                strlen (u.release));
      str_copy (info->arch, sizeof info->arch, u.machine,
                strlen (u.machine));
    }

  if (!((read_file ("/etc/os-release", p->buf, sizeof p->buf)
         || read_file ("/usr/lib/os-release", p->buf, sizeof p->buf))
        && line_value (p->buf, "PRETTY_NAME=", "", info->os,
                       sizeof info->os)))
    str_copy (info->os, sizeof info->os, "GNU/Linux", 9);

  static const char *const model_keys[] = {
    "model name", "Model", "Hardware", "cpu model", "Processor"
  };
  bool found = false;
  if (read_file ("/proc/cpuinfo", p->buf, sizeof p->buf))
    for (size_t i = 0; !found && i < ARRAY_CARDINALITY (model_keys); i++)
      found = line_value (p->buf, model_keys[i], " \t:", info->cpu_model,
                          sizeof info->cpu_model);
  if (!found)
    str_copy (info->cpu_model, sizeof info->cpu_model, info->arch,
              strlen (info->arch));

  long n = sysconf (_SC_NPROCESSORS_ONLN);
  info->ncpu = n > 0 ? (int) n : 1;
}

static void
linux_sample (struct source *s, struct sys_sample *out)
{
  struct linux_src *p = s->priv;
  unsigned long long total, idle;

  if (cpu_times (p, &total, &idle))
    {
      if (p->have_prev && total > p->prev_total)
        {
          double dt = (double) (total - p->prev_total);
          /* The iowait counter can go backwards.  */
          double di = (double) (long long) (idle - p->prev_idle);
          double pct = 100.0 * (1.0 - di / dt);
          out->cpu_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        }
      p->prev_total = total;
      p->prev_idle = idle;
      p->have_prev = true;
    }

  if (read_at0 (p->fd_meminfo, p->buf, sizeof p->buf) > 0)
    {
      double tot = field_kib (p->buf, "MemTotal");
      double avail = field_kib (p->buf, "MemAvailable");
      if (isnan (avail))
        avail = (field_kib (p->buf, "MemFree") + field_kib (p->buf, "Buffers")
                 + field_kib (p->buf, "Cached"));
      if (!isnan (tot) && !isnan (avail))
        {
          out->mem_total = tot / 1024;
          out->mem_used = (tot - avail) / 1024;
        }
      double st = field_kib (p->buf, "SwapTotal");
      double sf = field_kib (p->buf, "SwapFree");
      if (!isnan (st) && !isnan (sf))
        {
          out->swap_total = st / 1024;
          out->swap_used = (st - sf) / 1024;
        }
    }

  if (read_at0 (p->fd_loadavg, p->buf, sizeof p->buf) > 0)
    {
      double l[3];
      char *pos = p->buf, *end;
      int i;
      for (i = 0; i < 3; i++, pos = end)
        {
          l[i] = c_strtod (pos, &end);
          if (end == pos)
            break;
        }
      if (i == 3)
        memcpy (out->load, l, sizeof l);
    }

  if (read_at0 (p->fd_uptime, p->buf, sizeof p->buf) > 0)
    out->uptime = c_strtod (p->buf, NULL);
}

static void
linux_close (struct source *s)
{
  struct linux_src *p = s->priv;
  int fds[] = { p->fd_stat, p->fd_meminfo, p->fd_loadavg, p->fd_uptime };
  for (size_t i = 0; i < ARRAY_CARDINALITY (fds); i++)
    if (fds[i] >= 0)
      close (fds[i]);
  free (p);
}

const struct source_ops source_linux_ops = {
  linux_open, linux_info, linux_sample, linux_close
};

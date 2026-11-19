/* util.c -- small helpers: memory, file reading and formatting.
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
#include <error.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

static void
xalloc_die (void)
{
  error (EXIT_FAILURE, 0, "%s", _("memory exhausted"));
  abort ();
}

void *
xmalloc (size_t n)
{
  void *p = malloc (n ? n : 1);
  if (!p)
    xalloc_die ();
  return p;
}

void *
xcalloc (size_t n, size_t s)
{
  void *p = calloc (n ? n : 1, s ? s : 1);
  if (!p)
    xalloc_die ();
  return p;
}

/* Exit unsuccessfully if anything written to standard output was
   lost, as the GNU Coding Standards ask.  Registered with atexit.  */
void
close_stdout (void)
{
  bool failed = ferror (stdout) != 0;
  if (fclose (stdout) != 0)
    failed = true;
  if (failed)
    {
      error (0, errno, "%s", _("write error"));
      _exit (EXIT_FAILURE);
    }
}

void
str_copy (char *dst, size_t size, const char *src, size_t len)
{
  if (size == 0)
    return;
  if (len > size - 1)
    len = size - 1;
  memcpy (dst, src, len);
  dst[len] = '\0';
}

double
c_strtod (const char *s, char **end)
{
  static locale_t c_locale;
  if (!c_locale)
    {
      c_locale = newlocale (LC_NUMERIC_MASK, "C", (locale_t) 0);
      if (!c_locale)
        xalloc_die ();
    }
  return strtod_l (s, end, c_locale);
}

int
open_ro (const char *path)
{
  return open (path, O_RDONLY | O_CLOEXEC);
}

static const char *sysroot = "";

void
set_sysroot (const char *dir)
{
  sysroot = dir;
}

bool
sysroot_set (void)
{
  return sysroot[0] != '\0';
}

void
sys_path (char *buf, size_t size, const char *path)
{
  snprintf (buf, size, "%s%s", sysroot, path);
}

int
sys_open (const char *path)
{
  char full[4096];
  sys_path (full, sizeof full, path);
  return open_ro (full);
}

bool
sys_read (const char *path, char *buf, size_t size)
{
  char full[4096];
  sys_path (full, sizeof full, path);
  return read_file (full, buf, size);
}

bool
sys_number (const char *path, double *out)
{
  char buf[64], *end;
  if (!sys_read (path, buf, sizeof buf))
    return false;
  double v = c_strtod (buf, &end);
  if (end == buf)
    return false;
  *out = v;
  return true;
}

double
psi_avg10 (const char *buf, const char *kind)
{
  size_t len = strlen (kind);
  for (const char *line = buf; line && *line;)
    {
      if (strncmp (line, kind, len) == 0 && line[len] == ' ')
        {
          const char *v = strstr (line, "avg10=");
          return v ? c_strtod (v + 6, NULL) : NAN;
        }
      line = strchr (line, '\n');
      if (line)
        line++;
    }
  return NAN;
}

double
now_mono (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

ssize_t
read_at0 (int fd, char *buf, size_t size)
{
  size_t got = 0;

  if (fd < 0 || size == 0)
    return -1;
  /* Kernel files can hand out their contents a page at a time, so read
     until the end or until BUF is full.  */
  while (got < size - 1)
    {
      ssize_t n = pread (fd, buf + got, size - 1 - got, got);
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        return -1;
      if (n == 0)
        break;
      got += n;
    }
  buf[got] = '\0';
  return got;
}

bool
read_file (const char *path, char *buf, size_t size)
{
  int fd = open_ro (path);
  if (fd < 0)
    return false;
  ssize_t n = read_at0 (fd, buf, size);
  close (fd);
  return n > 0;
}

/* Compact span of time for graph axes: "45s", "2m", "2m30s", "1h05m".  */
void
fmt_duration (char *buf, size_t size, double seconds)
{
  long s = (long) (seconds + 0.5);
  if (s < 60)
    snprintf (buf, size, "%lds", s);
  else if (s < 3600)
    {
      if (s % 60)
        snprintf (buf, size, "%ldm%02lds", s / 60, s % 60);
      else
        snprintf (buf, size, "%ldm", s / 60);
    }
  else
    snprintf (buf, size, "%ldh%02ldm", s / 3600, s % 3600 / 60);
}

void
fmt_uptime (char *buf, size_t size, double seconds)
{
  if (isnan (seconds))
    {
      snprintf (buf, size, "%s", _("n/a"));
      return;
    }
  long s = (long) seconds;
  long d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
  if (d > 0)
    snprintf (buf, size,
              ngettext ("%ld day, %02ld:%02ld", "%ld days, %02ld:%02ld", d),
              d, h, m);
  else
    snprintf (buf, size, "%02ld:%02ld", h, m);
}

void
fmt_interval (char *buf, size_t size, double seconds)
{
  snprintf (buf, size, "%gs", seconds);
}

void
fmt_mib (char *buf, size_t size, double mib)
{
  if (isnan (mib))
    snprintf (buf, size, "%s", _("n/a"));
  else if (mib >= 1024)
    snprintf (buf, size, "%.1f GiB", mib / 1024);
  else if (mib > 0 && mib < 1)
    snprintf (buf, size, "%.0f KiB", mib * 1024);
  else
    snprintf (buf, size, "%.0f MiB", mib);
}

/* Sizes of disks and file systems, in decimal units as their makers
   use: "512 GB", "1.0 TB".  */
void
fmt_bytes (char *buf, size_t size, double bytes)
{
  static const char *const units[] = { "B", "kB", "MB", "GB", "TB", "PB" };
  if (isnan (bytes))
    {
      snprintf (buf, size, "%s", _("n/a"));
      return;
    }
  size_t u = 0;
  while (bytes >= 1000 && u + 1 < ARRAY_CARDINALITY (units))
    {
      bytes /= 1000;
      u++;
    }
  snprintf (buf, size, bytes < 10 && u > 0 ? "%.1f %s" : "%.0f %s",
            bytes, units[u]);
}

void
fmt_rate (char *buf, size_t size, double bps)
{
  if (isnan (bps))
    snprintf (buf, size, "%s", _("n/a"));
  else if (bps >= 1024.0 * 1024 * 1024)
    snprintf (buf, size, "%.2f GiB/s", bps / (1024.0 * 1024 * 1024));
  else if (bps >= 100.0 * 1024 * 1024)
    snprintf (buf, size, "%.0f MiB/s", bps / (1024.0 * 1024));
  else if (bps >= 1024.0 * 1024)
    snprintf (buf, size, "%.1f MiB/s", bps / (1024.0 * 1024));
  else if (bps >= 1024)
    snprintf (buf, size, "%.0f KiB/s", bps / 1024);
  else
    snprintf (buf, size, "%.0f B/s", bps);
}

void
fmt_count_rate (char *buf, size_t size, double per_second)
{
  if (isnan (per_second))
    snprintf (buf, size, "%s", _("n/a"));
  else if (per_second >= 1e6)
    snprintf (buf, size, "%.1fM/s", per_second / 1e6);
  else if (per_second >= 1e4)
    snprintf (buf, size, "%.0fk/s", per_second / 1e3);
  else if (per_second >= 1e3)
    snprintf (buf, size, "%.1fk/s", per_second / 1e3);
  else
    snprintf (buf, size, "%.0f/s", per_second);
}

void
fmt_mhz (char *buf, size_t size, double mhz)
{
  if (isnan (mhz))
    snprintf (buf, size, "%s", _("n/a"));
  else if (mhz >= 1000)
    snprintf (buf, size, "%.2f GHz", mhz / 1000);
  else
    snprintf (buf, size, "%.0f MHz", mhz);
}

/* "1.4 / 6.0 GiB" when both are in GiB, else "512 MiB / 6.0 GiB".  */
void
fmt_used_total (char *buf, size_t size, double used, double total)
{
  char u[32], t[32];
  if (isnan (used) || isnan (total))
    {
      snprintf (buf, size, "%s", _("n/a"));
      return;
    }
  if (used >= 1024 && total >= 1024)
    {
      snprintf (buf, size, "%.1f / %.1f GiB", used / 1024, total / 1024);
      return;
    }
  fmt_mib (u, sizeof u, used);
  fmt_mib (t, sizeof t, total);
  snprintf (buf, size, "%s / %s", u, t);
}

bool
utf8_locale (void)
{
  return strcmp (nl_langinfo (CODESET), "UTF-8") == 0;
}

void
fmt_temp (char *buf, size_t size, double celsius)
{
  if (isnan (celsius))
    snprintf (buf, size, "%s", _("n/a"));
  else
    snprintf (buf, size, "%.0f%s", celsius, utf8_locale () ? "°C" : "C");
}

void
fmt_pct (char *buf, size_t size, double pct)
{
  if (isnan (pct))
    snprintf (buf, size, "%s", _("n/a"));
  else
    snprintf (buf, size, "%.0f%%", pct);
}

void
fmt_watts (char *buf, size_t size, double watts)
{
  if (isnan (watts))
    snprintf (buf, size, "%s", _("n/a"));
  else
    snprintf (buf, size, "%.0f W", watts);
}

/* "17 / 125 W", or "17 W" when there is no limit.  */
void
fmt_of_limit (char *buf, size_t size, double v, double limit,
              const char *unit)
{
  if (isnan (v))
    snprintf (buf, size, "%s", _("n/a"));
  else if (isnan (limit))
    snprintf (buf, size, "%.0f %s", v, unit);
  else
    snprintf (buf, size, "%.0f / %.0f %s", v, limit, unit);
}

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
#include <locale.h>
#include <stdio.h>
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

ssize_t
read_at0 (int fd, char *buf, size_t size)
{
  ssize_t n;

  if (fd < 0 || size == 0)
    return -1;
  do
    n = pread (fd, buf, size - 1, 0);
  while (n < 0 && errno == EINTR);
  if (n < 0)
    return -1;
  buf[n] = '\0';
  return n;
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
  else
    snprintf (buf, size, "%.0f MiB", mib);
}

void
fmt_used_total (char *buf, size_t size, double used, double total)
{
  char u[32], t[32];
  if (isnan (used) || isnan (total))
    {
      snprintf (buf, size, "%s", _("n/a"));
      return;
    }
  fmt_mib (u, sizeof u, used);
  fmt_mib (t, sizeof t, total);
  snprintf (buf, size, "%s / %s", u, t);
}

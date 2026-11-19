/* source_linux.c -- facts about the running GNU/Linux system.
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

#include <config.h>

#include "system.h"

#include <errno.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "source.h"
#include "util.h"

struct linux_src
{
  int fd_loadavg, fd_uptime;
  char buf[16384];
};

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

/* Read the one-line system file PATH into DST.  */
static bool
read_line (const char *path, char *dst, size_t size)
{
  if (!sys_read (path, dst, size))
    return false;
  dst[strcspn (dst, "\n")] = '\0';
  return dst[0] != '\0';
}

static bool
linux_open (struct source *s)
{
  struct linux_src *p = xcalloc (1, sizeof *p);

  p->fd_loadavg = sys_open ("/proc/loadavg");
  if (p->fd_loadavg < 0)
    {
      int e = errno;
      free (p);
      errno = e;
      return false;
    }
  p->fd_uptime = sys_open ("/proc/uptime");
  s->priv = p;
  return true;
}

static void
linux_info (struct source *s, struct sys_info *info)
{
  struct linux_src *p = s->priv;
  struct utsname u;

  /* The host name and kernel release come from /proc, so that a copy
     of it describes the system it was taken from.  */
  if (!read_line ("/proc/sys/kernel/hostname", info->host, sizeof info->host)
      && gethostname (info->host, sizeof info->host - 1) != 0)
    str_copy (info->host, sizeof info->host, "?", 1);
  bool have_uname = uname (&u) == 0;
  if (!read_line ("/proc/sys/kernel/osrelease", info->kernel,
                  sizeof info->kernel) && have_uname)
    str_copy (info->kernel, sizeof info->kernel, u.release,
              strlen (u.release));
  if (have_uname)
    str_copy (info->arch, sizeof info->arch, u.machine, strlen (u.machine));

  if (!((sys_read ("/etc/os-release", p->buf, sizeof p->buf)
         || sys_read ("/usr/lib/os-release", p->buf, sizeof p->buf))
        && line_value (p->buf, "PRETTY_NAME=", "", info->os,
                       sizeof info->os)))
    str_copy (info->os, sizeof info->os, "GNU/Linux", 9);

  static const char *const model_keys[] = {
    "model name", "Model", "Hardware", "cpu model", "Processor"
  };
  bool found = false;
  if (sys_read ("/proc/cpuinfo", p->buf, sizeof p->buf))
    for (size_t i = 0; !found && i < ARRAY_CARDINALITY (model_keys); i++)
      found = line_value (p->buf, model_keys[i], " \t:", info->cpu_model,
                          sizeof info->cpu_model);
  if (!found)
    str_copy (info->cpu_model, sizeof info->cpu_model, info->arch,
              strlen (info->arch));
}

static void
linux_sample (struct source *s, struct sys_sample *out)
{
  struct linux_src *p = s->priv;

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
  if (p->fd_loadavg >= 0)
    close (p->fd_loadavg);
  if (p->fd_uptime >= 0)
    close (p->fd_uptime);
  free (p);
}

const struct source_ops source_linux_ops = {
  linux_open, linux_info, linux_sample, linux_close
};

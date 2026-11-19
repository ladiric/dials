/* log.c -- writing measurements to a file.
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
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "log.h"
#include "util.h"

static bool
ends_with (const char *s, const char *suffix)
{
  size_t n = strlen (s), k = strlen (suffix);
  return n >= k && strcmp (s + n - k, suffix) == 0;
}

/* Write V with DIGITS decimals, or nothing in CSV and null in JSON if
   it is unknown.  */
static void
number (const struct logger *l, double v, int digits)
{
  if (isnan (v) || isinf (v))
    fputs (l->json ? "null" : "", l->f);
  else
    c_fprintf (l->f, "%.*f", digits, v);
}

/* Write S as a JSON string.  */
static void
json_string (FILE *f, const char *s)
{
  putc ('"', f);
  for (; *s; s++)
    {
      unsigned char c = (unsigned char) *s;
      if (c == '"' || c == '\\')
        fprintf (f, "\\%c", c);
      else if (c < 0x20)
        fprintf (f, "\\u%04x", c);
      else
        putc (c, f);
    }
  putc ('"', f);
}

static const struct disk_dev *
find_disk (const struct app *a, const char *name)
{
  for (int i = 0; i < a->disks.n; i++)
    if (strcmp (a->disks.dev[i].name, name) == 0)
      return &a->disks.dev[i];
  return NULL;
}

static const struct net_if *
find_net (const struct app *a, const char *name)
{
  for (int i = 0; i < a->net.n; i++)
    if (strcmp (a->net.dev[i].name, name) == 0)
      return &a->net.dev[i];
  return NULL;
}

int
log_start (struct logger *l, const char *path, const struct app *a)
{
  memset (l, 0, sizeof *l);
  snprintf (l->path, sizeof l->path, "%s", path);
  l->json = ends_with (path, ".json") || ends_with (path, ".jsonl");

  struct stat st;
  bool fresh = stat (path, &st) != 0 || st.st_size == 0;
  l->f = fopen (path, "ae");
  if (!l->f)
    return errno;

  for (int i = 0; i < a->disks.n; i++)
    snprintf (l->disks[l->ndisk++], sizeof l->disks[0], "%s",
              a->disks.dev[i].name);
  /* Interfaces backed by hardware; virtual ones come and go.  */
  for (int i = 0; i < a->net.n; i++)
    if (a->net.dev[i].physical)
      snprintf (l->nets[l->nnet++], sizeof l->nets[0], "%s",
                a->net.dev[i].name);

  if (!l->json && fresh)
    {
      fputs ("time,cpu_percent,memory_used_mib,memory_total_mib,"
             "swap_used_mib,load1", l->f);
      for (int i = 0; i < a->gpus.count; i++)
        fprintf (l->f, ",gpu%d_temp_c,gpu%d_util_percent,"
                 "gpu%d_memory_used_mib,gpu%d_power_w", i, i, i, i);
      for (int i = 0; i < l->ndisk; i++)
        fprintf (l->f, ",disk_%s_read_bps,disk_%s_write_bps",
                 l->disks[i], l->disks[i]);
      for (int i = 0; i < l->nnet; i++)
        fprintf (l->f, ",net_%s_rx_bps,net_%s_tx_bps", l->nets[i], l->nets[i]);
      putc ('\n', l->f);
    }
  if (fflush (l->f) != 0)
    {
      int err = errno;
      log_stop (l);
      return err;
    }
  return 0;
}

static void
write_csv (struct logger *l, const struct app *a, const char *when)
{
  FILE *f = l->f;
  fputs (when, f);
  double vals[] = { a->cpu.total, a->mem.used, a->mem.total,
                    a->mem.swap_used, a->cur.load[0] };
  int digits[] = { 1, 1, 1, 1, 2 };
  for (size_t k = 0; k < ARRAY_CARDINALITY (vals); k++)
    {
      putc (',', f);
      number (l, vals[k], digits[k]);
    }
  for (int i = 0; i < a->gpus.count; i++)
    {
      const struct gpu_sample *s = &a->gpus.dev[i].cur;
      double g[] = { s->temp, s->util, s->mem_used, s->power };
      for (size_t k = 0; k < ARRAY_CARDINALITY (g); k++)
        {
          putc (',', f);
          number (l, g[k], 1);
        }
    }
  for (int i = 0; i < l->ndisk; i++)
    {
      const struct disk_dev *d = find_disk (a, l->disks[i]);
      putc (',', f);
      number (l, d ? d->rd_bps : NAN, 0);
      putc (',', f);
      number (l, d ? d->wr_bps : NAN, 0);
    }
  for (int i = 0; i < l->nnet; i++)
    {
      const struct net_if *n = find_net (a, l->nets[i]);
      putc (',', f);
      number (l, n ? n->rx_bps : NAN, 0);
      putc (',', f);
      number (l, n ? n->tx_bps : NAN, 0);
    }
  putc ('\n', f);
}

static void
write_json (struct logger *l, const struct app *a, const char *when)
{
  FILE *f = l->f;
  fputs ("{\"time\":", f);
  json_string (f, when);
  fputs (",\"cpu_percent\":", f);
  number (l, a->cpu.total, 1);
  fputs (",\"memory_used_mib\":", f);
  number (l, a->mem.used, 1);
  fputs (",\"memory_total_mib\":", f);
  number (l, a->mem.total, 1);
  fputs (",\"swap_used_mib\":", f);
  number (l, a->mem.swap_used, 1);
  fputs (",\"load\":[", f);
  for (int k = 0; k < 3; k++)
    {
      if (k)
        putc (',', f);
      number (l, a->cur.load[k], 2);
    }
  fputs ("],\"gpus\":[", f);
  for (int i = 0; i < a->gpus.count; i++)
    {
      const struct gpu_sample *s = &a->gpus.dev[i].cur;
      fputs (i ? ",{\"name\":" : "{\"name\":", f);
      json_string (f, a->gpus.dev[i].name);
      fputs (",\"temp_c\":", f);
      number (l, s->temp, 1);
      fputs (",\"util_percent\":", f);
      number (l, s->util, 1);
      fputs (",\"memory_used_mib\":", f);
      number (l, s->mem_used, 1);
      fputs (",\"power_w\":", f);
      number (l, s->power, 1);
      putc ('}', f);
    }
  fputs ("],\"disks\":{", f);
  for (int i = 0; i < l->ndisk; i++)
    {
      const struct disk_dev *d = find_disk (a, l->disks[i]);
      if (i)
        putc (',', f);
      json_string (f, l->disks[i]);
      fputs (":{\"read_bps\":", f);
      number (l, d ? d->rd_bps : NAN, 0);
      fputs (",\"write_bps\":", f);
      number (l, d ? d->wr_bps : NAN, 0);
      putc ('}', f);
    }
  fputs ("},\"net\":{", f);
  for (int i = 0; i < l->nnet; i++)
    {
      const struct net_if *n = find_net (a, l->nets[i]);
      if (i)
        putc (',', f);
      json_string (f, l->nets[i]);
      fputs (":{\"rx_bps\":", f);
      number (l, n ? n->rx_bps : NAN, 0);
      fputs (",\"tx_bps\":", f);
      number (l, n ? n->tx_bps : NAN, 0);
      putc ('}', f);
    }
  fputs ("}}\n", f);
}

int
log_write (struct logger *l, const struct app *a)
{
  if (!l->f)
    return 0;

  /* Local time with its offset from UTC, as ISO 8601 has it.  */
  char when[64];
  time_t now = time (NULL);
  struct tm tm;
  localtime_r (&now, &tm);
  strftime (when, sizeof when, "%Y-%m-%dT%H:%M:%S%z", &tm);

  if (l->json)
    write_json (l, a, when);
  else
    write_csv (l, a, when);
  /* Flush each line, so that the file can be followed as it grows.  */
  if (fflush (l->f) != 0 || ferror (l->f))
    return errno ? errno : EIO;
  return 0;
}

void
log_stop (struct logger *l)
{
  if (l->f)
    fclose (l->f);
  l->f = NULL;
}

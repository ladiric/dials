/* sensors.c -- hardware monitoring sensors.
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

#include "sensors.h"
#include "util.h"

/* The kinds of hwmon inputs read: the file name prefix, the divisor
   that gives the unit, and the files that hold a limit, best first.  */
static const struct
{
  const char *prefix;
  enum sensor_kind kind;
  double scale;
  const char *limits[2];
} kinds[] = {
  { "temp", SENSOR_TEMP, 1000, { "crit", "max" } },
  { "fan", SENSOR_FAN, 1, { "max", NULL } },
  { "in", SENSOR_VOLTAGE, 1000, { "max", NULL } },
  { "curr", SENSOR_CURRENT, 1000, { "max", NULL } },
  { "power", SENSOR_POWER, 1e6, { "cap", "max" } },
};

static double
rnd (struct sensors *s)
{
  s->rng ^= s->rng << 13;
  s->rng ^= s->rng >> 7;
  s->rng ^= s->rng << 17;
  return (double) (s->rng >> 11) / 9007199254740992.0;
}

void
fmt_sensor (char *buf, size_t size, enum sensor_kind k, double v)
{
  if (isnan (v))
    {
      snprintf (buf, size, "%s", _("n/a"));
      return;
    }
  switch (k)
    {
    case SENSOR_TEMP:
      fmt_temp (buf, size, v);
      break;
    case SENSOR_FAN:
      snprintf (buf, size, _("%.0f RPM"), v);
      break;
    case SENSOR_VOLTAGE:
      snprintf (buf, size, "%.2f V", v);
      break;
    case SENSOR_CURRENT:
      snprintf (buf, size, "%.2f A", v);
      break;
    case SENSOR_POWER:
      snprintf (buf, size, "%.1f W", v);
      break;
    }
}

/* Return the number in an hwmon directory name such as "hwmon12", for
   putting them in order.  */
static int
hwmon_number (const char *name)
{
  return atoi (name + 5);
}

static int
by_hwmon_number (const void *a, const void *b)
{
  return hwmon_number (*(char *const *) a) - hwmon_number (*(char *const *) b);
}

struct input
{
  int kind, number;
  bool average;			/* power1_average rather than power1_input.  */
};

static int
by_kind_and_number (const void *a, const void *b)
{
  const struct input *x = a, *y = b;
  if (x->kind != y->kind)
    return x->kind - y->kind;
  return x->number - y->number;
}

/* If NAME is an input file such as "temp3_input", describe it in IN.  */
static bool
parse_input (const char *name, struct input *in)
{
  for (size_t k = 0; k < ARRAY_CARDINALITY (kinds); k++)
    {
      size_t len = strlen (kinds[k].prefix);
      if (strncmp (name, kinds[k].prefix, len) != 0
          || name[len] < '0' || name[len] > '9')
        continue;
      char *end;
      long num = strtol (name + len, &end, 10);
      if (strcmp (end, "_input") == 0)
        in->average = false;
      else if (kinds[k].kind == SENSOR_POWER && strcmp (end, "_average") == 0)
        in->average = true;
      else
        return false;
      in->kind = (int) k;
      in->number = (int) num;
      return true;
    }
  return false;
}

static void
add_chip (struct sensors *s, const char *dir, const char *chip)
{
  char rel[512], path[4096];
  snprintf (rel, sizeof rel, "/sys/class/hwmon/%s", dir);
  sys_path (path, sizeof path, rel);
  DIR *d = opendir (path);
  if (!d)
    return;

  struct input inputs[SENSOR_MAX];
  int n = 0;
  struct dirent *e;
  while ((e = readdir (d)) && n < SENSOR_MAX)
    if (parse_input (e->d_name, &inputs[n]))
      {
        /* Prefer power1_input to power1_average.  */
        bool dup = false;
        for (int i = 0; i < n; i++)
          if (inputs[i].kind == inputs[n].kind
              && inputs[i].number == inputs[n].number)
            {
              dup = true;
              if (inputs[i].average)
                inputs[i].average = inputs[n].average;
            }
        if (!dup)
          n++;
      }
  closedir (d);
  qsort (inputs, n, sizeof inputs[0], by_kind_and_number);

  for (int i = 0; i < n && s->n < SENSOR_MAX; i++)
    {
      const char *prefix = kinds[inputs[i].kind].prefix;
      char file[640], buf[64];
      snprintf (file, sizeof file, "%s/%s%d_%s", rel, prefix,
                inputs[i].number, inputs[i].average ? "average" : "input");
      int fd = sys_open (file);
      /* Some drivers list inputs that cannot be read, such as missing
         fans; leave those out.  */
      if (fd < 0 || read_at0 (fd, buf, sizeof buf) <= 0)
        {
          if (fd >= 0)
            close (fd);
          continue;
        }

      struct sensor *sn = &s->s[s->n++];
      memset (sn, 0, sizeof *sn);
      sn->fd = fd;
      sn->kind = kinds[inputs[i].kind].kind;
      sn->scale = kinds[inputs[i].kind].scale;
      sn->value = sn->lo = sn->hi = NAN;
      str_copy (sn->chip, sizeof sn->chip, chip, strlen (chip));
      snprintf (file, sizeof file, "%s/%s%d_label", rel, prefix,
                inputs[i].number);
      if (sys_read (file, buf, sizeof buf))
        {
          buf[strcspn (buf, "\n")] = '\0';
          str_copy (sn->label, sizeof sn->label, buf, strlen (buf));
        }
      else
        snprintf (sn->label, sizeof sn->label, "%s%d", prefix,
                  inputs[i].number);
      sn->limit = NAN;
      for (int l = 0; l < 2 && kinds[inputs[i].kind].limits[l]; l++)
        {
          double v;
          snprintf (file, sizeof file, "%s/%s%d_%s", rel, prefix,
                    inputs[i].number, kinds[inputs[i].kind].limits[l]);
          if (sys_number (file, &v) && v > 0)
            {
              sn->limit = v / sn->scale;
              break;
            }
        }
    }
}

static void
add_demo (struct sensors *s, size_t history)
{
  static const struct
  {
    const char *chip, *label;
    enum sensor_kind kind;
    double value, limit;
  } demo[] = {
    { "coretemp", "Package id 0", SENSOR_TEMP, 52, 100 },
    { "coretemp", "Core 0", SENSOR_TEMP, 49, 100 },
    { "coretemp", "Core 1", SENSOR_TEMP, 51, 100 },
    { "nvme", "Composite", SENSOR_TEMP, 41, 84.85 },
    { "nct6798", "fan1", SENSOR_FAN, 820, NAN },
    { "nct6798", "fan2", SENSOR_FAN, 1150, NAN },
    { "nct6798", "in0", SENSOR_VOLTAGE, 1.02, NAN },
    { "nct6798", "+12V", SENSOR_VOLTAGE, 12.1, NAN },
    { "psu", "total power", SENSOR_POWER, 180, 850 },
  };
  for (size_t i = 0; i < ARRAY_CARDINALITY (demo); i++)
    {
      struct sensor *sn = &s->s[s->n++];
      memset (sn, 0, sizeof *sn);
      snprintf (sn->chip, sizeof sn->chip, "%s", demo[i].chip);
      snprintf (sn->label, sizeof sn->label, "%s", demo[i].label);
      sn->kind = demo[i].kind;
      sn->value = demo[i].value;
      sn->lo = sn->hi = NAN;
      sn->limit = demo[i].limit;
      sn->fd = -1;
      sn->scale = demo[i].value;	/* Simulated readings wobble around this.  */
      metric_init (&sn->m, history);
    }
}

void
sensors_open (struct sensors *s, bool demo, size_t history)
{
  memset (s, 0, sizeof *s);
  s->demo = demo;
  s->rng = UINT64_C (0x13198A2E03707344);
  if (demo)
    add_demo (s, history);
  else
    {
      char path[4096];
      sys_path (path, sizeof path, "/sys/class/hwmon");
      DIR *d = opendir (path);
      char *dirs[256];
      int nd = 0;
      if (d)
        {
          struct dirent *e;
          while ((e = readdir (d)) && nd < 256)
            if (strncmp (e->d_name, "hwmon", 5) == 0)
              {
                dirs[nd] = xmalloc (strlen (e->d_name) + 1);
                strcpy (dirs[nd++], e->d_name);
              }
          closedir (d);
        }
      qsort (dirs, nd, sizeof dirs[0], by_hwmon_number);

      for (int i = 0; i < nd; i++)
        {
          char rel[512], name[40], chip[56];
          snprintf (rel, sizeof rel, "/sys/class/hwmon/%s/name", dirs[i]);
          if (!sys_read (rel, name, sizeof name))
            snprintf (name, sizeof name, "%s", dirs[i]);
          name[strcspn (name, "\n")] = '\0';

          /* Number chips that share a driver, such as one per memory
             module: "jc42", "jc42 #2", ...  */
          int same = 0;
          for (int k = 0; k < i; k++)
            {
              char other[40];
              snprintf (rel, sizeof rel, "/sys/class/hwmon/%s/name", dirs[k]);
              if (sys_read (rel, other, sizeof other))
                {
                  other[strcspn (other, "\n")] = '\0';
                  same += strcmp (other, name) == 0;
                }
            }
          if (same)
            snprintf (chip, sizeof chip, "%.30s #%d", name, same + 1);
          else
            snprintf (chip, sizeof chip, "%s", name);
          add_chip (s, dirs[i], chip);
          free (dirs[i]);
        }
      for (int i = 0; i < s->n; i++)
        metric_init (&s->s[i].m, history);
    }
  sensors_sample (s);
}

void
sensors_sample (struct sensors *s)
{
  for (int i = 0; i < s->n; i++)
    {
      struct sensor *sn = &s->s[i];
      if (s->demo)
        {
          double wobble = sn->kind == SENSOR_FAN ? 30 : sn->kind == SENSOR_TEMP
            ? 1.5 : sn->kind == SENSOR_POWER ? 25 : 0.02;
          sn->value = sn->scale + (rnd (s) * 2 - 1) * wobble;
        }
      else
        {
          char buf[64];
          sn->value = read_at0 (sn->fd, buf, sizeof buf) > 0
            ? c_strtod (buf, NULL) / sn->scale : NAN;
        }
      if (!isnan (sn->value))
        {
          if (isnan (sn->lo) || sn->value < sn->lo)
            sn->lo = sn->value;
          if (isnan (sn->hi) || sn->value > sn->hi)
            sn->hi = sn->value;
        }
      metric_push (&sn->m, sn->value);
    }
}

void
sensors_clear_history (struct sensors *s)
{
  for (int i = 0; i < s->n; i++)
    {
      metric_clear (&s->s[i].m);
      s->s[i].lo = s->s[i].hi = s->s[i].value;
    }
}

void
sensors_close (struct sensors *s)
{
  for (int i = 0; i < s->n; i++)
    {
      if (s->s[i].fd >= 0)
        close (s->s[i].fd);
      metric_free (&s->s[i].m);
    }
}

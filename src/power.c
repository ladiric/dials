/* power.c -- batteries, power supplies and processor power.
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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "power.h"
#include "util.h"

static double
rnd (struct power *p)
{
  p->rng ^= p->rng << 13;
  p->rng ^= p->rng >> 7;
  p->rng ^= p->rng << 17;
  return (double) (p->rng >> 11) / 9007199254740992.0;
}

/* Read the one-line file NAME of power supply DIR into BUF.  */
static bool
supply_text (const char *dir, const char *name, char *buf, size_t size)
{
  char rel[512];
  snprintf (rel, sizeof rel, "/sys/class/power_supply/%s/%s", dir, name);
  if (!sys_read (rel, buf, size))
    return false;
  buf[strcspn (buf, "\n")] = '\0';
  return buf[0] != '\0';
}

/* Read the number in file NAME of power supply DIR, divided by SCALE,
   or NaN.  */
static double
supply_number (const char *dir, const char *name, double scale)
{
  char rel[512];
  double v;
  snprintf (rel, sizeof rel, "/sys/class/power_supply/%s/%s", dir, name);
  return sys_number (rel, &v) ? v / scale : NAN;
}

static void
read_battery (struct battery *b)
{
  const char *d = b->name;
  char buf[64];

  b->status[0] = b->level[0] = '\0';
  if (supply_text (d, "status", buf, sizeof buf))
    str_copy (b->status, sizeof b->status, buf, strlen (buf));
  if (supply_text (d, "capacity_level", buf, sizeof buf))
    str_copy (b->level, sizeof b->level, buf, strlen (buf));
  b->capacity = supply_number (d, "capacity", 1);
  b->voltage = supply_number (d, "voltage_now", 1e6);
  b->cycles = supply_number (d, "cycle_count", 1);

  /* Energy is given in microwatt-hours, or as charge in microamp-hours
     to be multiplied by the design voltage.  */
  b->energy = supply_number (d, "energy_now", 1e6);
  b->energy_full = supply_number (d, "energy_full", 1e6);
  b->energy_design = supply_number (d, "energy_full_design", 1e6);
  if (isnan (b->energy))
    {
      double volts = supply_number (d, "voltage_min_design", 1e6);
      if (isnan (volts))
        volts = b->voltage;
      b->energy = supply_number (d, "charge_now", 1e6) * volts;
      b->energy_full = supply_number (d, "charge_full", 1e6) * volts;
      b->energy_design = supply_number (d, "charge_full_design", 1e6) * volts;
    }
  b->power = supply_number (d, "power_now", 1e6);
  if (isnan (b->power))
    b->power = supply_number (d, "current_now", 1e6) * b->voltage;
  if (b->power < 0)		/* Some drivers sign the discharge rate.  */
    b->power = -b->power;
  if (isnan (b->capacity) && b->energy_full > 0)
    b->capacity = 100 * b->energy / b->energy_full;

  b->time_left = NAN;
  if (b->power > 0.1)
    {
      if (streq (b->status, "Discharging"))
        b->time_left = b->energy / b->power * 3600;
      else if (streq (b->status, "Charging"))
        b->time_left = (b->energy_full - b->energy) / b->power * 3600;
    }
}

static void
add_supplies (struct power *p, size_t history)
{
  char path[4096];
  sys_path (path, sizeof path, "/sys/class/power_supply");
  DIR *dir = opendir (path);
  if (!dir)
    return;
  struct dirent *e;
  while ((e = readdir (dir)))
    {
      char type[32], buf[64];
      if (e->d_name[0] == '.' || !supply_text (e->d_name, "type", type,
                                               sizeof type))
        continue;
      if (streq (type, "Battery") && p->nbat < BATTERY_MAX)
        {
          struct battery *b = &p->bat[p->nbat++];
          memset (b, 0, sizeof *b);
          str_copy (b->name, sizeof b->name, e->d_name, strlen (e->d_name));
          if (supply_text (e->d_name, "model_name", buf, sizeof buf))
            str_copy (b->model, sizeof b->model, buf, strlen (buf));
          if (supply_text (e->d_name, "technology", buf, sizeof buf))
            str_copy (b->technology, sizeof b->technology, buf, strlen (buf));
          b->device = supply_text (e->d_name, "scope", buf, sizeof buf)
            && streq (buf, "Device");
          metric_init (&b->m_capacity, history);
        }
      else if ((streq (type, "Mains") || streq (type, "USB"))
               && p->nsup < SUPPLY_MAX)
        {
          struct supply *s = &p->sup[p->nsup++];
          str_copy (s->name, sizeof s->name, e->d_name, strlen (e->d_name));
          str_copy (s->type, sizeof s->type, type, strlen (type));
        }
    }
  closedir (dir);
}

static int
by_rapl_name (const void *a, const void *b)
{
  return strcmp (*(char *const *) a, *(char *const *) b);
}

/* The RAPL zones, such as intel-rapl:0 for a package and intel-rapl:0:2
   for its memory.  Their energy counters can be read only by root on
   most systems now.  */
static void
add_rapl (struct power *p, size_t history)
{
  char path[4096];
  sys_path (path, sizeof path, "/sys/class/powercap");
  DIR *dir = opendir (path);
  if (!dir)
    return;
  char *names[RAPL_MAX];
  int n = 0;
  struct dirent *e;
  while ((e = readdir (dir)) && n < RAPL_MAX)
    if (strncmp (e->d_name, "intel-rapl:", 11) == 0)
      {
        names[n] = xmalloc (strlen (e->d_name) + 1);
        strcpy (names[n++], e->d_name);
      }
  closedir (dir);
  qsort (names, n, sizeof names[0], by_rapl_name);

  for (int i = 0; i < n; i++)
    {
      char rel[512], buf[64];
      snprintf (rel, sizeof rel, "/sys/class/powercap/%s/energy_uj", names[i]);
      int fd = sys_open (rel);
      if (fd < 0)
        {
          if (errno == EACCES || errno == EPERM)
            p->rapl_denied = true;
          free (names[i]);
          continue;
        }
      struct rapl_zone *z = &p->rapl[p->nrapl++];
      memset (z, 0, sizeof *z);
      z->fd = fd;
      z->watts = NAN;
      z->depth = 0;
      for (const char *c = names[i] + 11; *c; c++)
        z->depth += *c == ':';
      snprintf (rel, sizeof rel, "/sys/class/powercap/%s/name", names[i]);
      if (sys_read (rel, buf, sizeof buf))
        {
          buf[strcspn (buf, "\n")] = '\0';
          str_copy (z->name, sizeof z->name, buf, strlen (buf));
        }
      else
        str_copy (z->name, sizeof z->name, names[i], strlen (names[i]));
      snprintf (rel, sizeof rel,
                "/sys/class/powercap/%s/max_energy_range_uj", names[i]);
      if (!sys_number (rel, &z->range))
        z->range = 0;
      metric_init (&z->m, history);
      free (names[i]);
    }
}

static void
demo_open (struct power *p, size_t history)
{
  struct battery *b = &p->bat[p->nbat++];
  memset (b, 0, sizeof *b);
  snprintf (b->name, sizeof b->name, "%s", "BAT0");
  snprintf (b->model, sizeof b->model, "%s", "Demo Battery");
  snprintf (b->technology, sizeof b->technology, "%s", "Li-ion");
  b->energy_full = 52;
  b->energy_design = 57;
  b->energy = 41;
  b->cycles = 312;
  metric_init (&b->m_capacity, history);

  b = &p->bat[p->nbat++];
  memset (b, 0, sizeof *b);
  snprintf (b->name, sizeof b->name, "%s", "mouse_battery");
  snprintf (b->model, sizeof b->model, "%s", "Demo Wireless Mouse");
  b->device = true;
  metric_init (&b->m_capacity, history);

  struct supply *s = &p->sup[p->nsup++];
  snprintf (s->name, sizeof s->name, "%s", "AC");
  snprintf (s->type, sizeof s->type, "%s", "Mains");

  static const char *const zones[] = { "package-0", "core", "uncore", "dram" };
  for (size_t i = 0; i < ARRAY_CARDINALITY (zones); i++)
    {
      struct rapl_zone *z = &p->rapl[p->nrapl++];
      memset (z, 0, sizeof *z);
      snprintf (z->name, sizeof z->name, "%s", zones[i]);
      z->depth = i > 0;
      z->fd = -1;
      metric_init (&z->m, history);
    }
}

static void
demo_sample (struct power *p, double dt)
{
  struct battery *b = &p->bat[0];
  b->power = 9 + rnd (p) * 6;
  b->energy -= b->power * dt / 3600;
  if (b->energy < 5)
    b->energy = b->energy_full;
  b->capacity = 100 * b->energy / b->energy_full;
  b->voltage = 11.4 + b->capacity / 100;
  b->time_left = b->energy / b->power * 3600;
  snprintf (b->status, sizeof b->status, "%s", "Discharging");

  b = &p->bat[1];
  b->capacity = b->energy = b->energy_full = b->power = NAN;
  b->voltage = b->cycles = b->time_left = b->energy_design = NAN;
  snprintf (b->status, sizeof b->status, "%s", "Discharging");
  snprintf (b->level, sizeof b->level, "%s", "Normal");

  p->sup[0].online = false;
  double pkg = 8 + rnd (p) * 30;
  p->rapl[0].watts = pkg;
  p->rapl[1].watts = pkg * 0.6;
  p->rapl[2].watts = pkg * 0.1;
  p->rapl[3].watts = 1.5 + rnd (p);
}

void
power_open (struct power *p, bool demo, size_t history)
{
  memset (p, 0, sizeof *p);
  p->demo = demo;
  p->rng = UINT64_C (0xA4093822299F31D0);
  if (demo)
    demo_open (p, history);
  else
    {
      add_supplies (p, history);
      add_rapl (p, history);
    }
  p->prev_time = now_mono ();
  power_sample (p);		/* Power needs a first reading.  */
  power_clear_history (p);
}

void
power_sample (struct power *p)
{
  double now = now_mono (), dt = now - p->prev_time;
  p->prev_time = now;

  if (p->demo)
    demo_sample (p, dt);
  else
    {
      for (int i = 0; i < p->nbat; i++)
        read_battery (&p->bat[i]);
      for (int i = 0; i < p->nsup; i++)
        {
          char buf[16];
          p->sup[i].online = supply_text (p->sup[i].name, "online", buf,
                                          sizeof buf) && buf[0] == '1';
        }
      for (int i = 0; i < p->nrapl; i++)
        {
          struct rapl_zone *z = &p->rapl[i];
          char buf[64];
          z->watts = NAN;
          if (read_at0 (z->fd, buf, sizeof buf) <= 0)
            continue;
          unsigned long long uj = strtoull (buf, NULL, 10);
          if (z->have_prev && dt > 0)
            {
              /* The counter wraps around at RANGE.  */
              double d = uj >= z->prev ? (double) (uj - z->prev)
                : z->range - z->prev + uj;
              z->watts = d / 1e6 / dt;
            }
          z->prev = uj;
          z->have_prev = true;
        }
    }

  for (int i = 0; i < p->nbat; i++)
    metric_push (&p->bat[i].m_capacity, p->bat[i].capacity);
  for (int i = 0; i < p->nrapl; i++)
    metric_push (&p->rapl[i].m, p->rapl[i].watts);
}

void
power_clear_history (struct power *p)
{
  for (int i = 0; i < p->nbat; i++)
    metric_clear (&p->bat[i].m_capacity);
  for (int i = 0; i < p->nrapl; i++)
    metric_clear (&p->rapl[i].m);
}

void
power_close (struct power *p)
{
  for (int i = 0; i < p->nbat; i++)
    metric_free (&p->bat[i].m_capacity);
  for (int i = 0; i < p->nrapl; i++)
    {
      if (p->rapl[i].fd >= 0)
        close (p->rapl[i].fd);
      metric_free (&p->rapl[i].m);
    }
}

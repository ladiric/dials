/* power.h -- batteries, power supplies and processor power.
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

#ifndef DIALS_POWER_H
#define DIALS_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum
{
  BATTERY_MAX = 8,
  SUPPLY_MAX = 8,
  RAPL_MAX = 16
};

/* A battery: the system's own, or a device's such as a mouse.
   Readings are NaN when unknown.  */
struct battery
{
  char name[32];
  char model[64];
  char status[24];		/* "Charging", "Discharging", "Full", ...  */
  char level[16];		/* "Normal", "Low", ... when there is no
                                   percentage.  */
  char technology[16];
  bool device;			/* Powers a device, not the system.  */
  double capacity;		/* Percent charged.  */
  double energy, energy_full, energy_design;	/* Watt-hours.  */
  double power;			/* Watts in or out.  */
  double voltage;		/* Volts.  */
  double cycles;		/* Charge cycles.  */
  double time_left;		/* Seconds to empty, or to full.  */
  struct metric m_capacity;
};

/* A mains or USB power supply.  */
struct supply
{
  char name[32];
  char type[16];
  bool online;
};

/* A RAPL power zone, such as a processor package or its memory.  */
struct rapl_zone
{
  char name[32];
  int depth;			/* 0 for a package, 1 for its parts.  */
  double watts;
  struct metric m;

  /* Private.  */
  int fd;
  double range;			/* Microjoules before the counter wraps.  */
  unsigned long long prev;
  bool have_prev;
};

struct power
{
  int nbat;
  struct battery bat[BATTERY_MAX];
  int nsup;
  struct supply sup[SUPPLY_MAX];
  int nrapl;
  struct rapl_zone rapl[RAPL_MAX];
  bool rapl_denied;		/* Zones exist, but reading needs root.  */

  /* Private.  */
  bool demo;
  uint64_t rng;
  double prev_time;
};

void power_open (struct power *p, bool demo, size_t history);
void power_sample (struct power *p);
void power_clear_history (struct power *p);
void power_close (struct power *p);

#endif /* DIALS_POWER_H */

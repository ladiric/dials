/* sensors.h -- hardware monitoring sensors.
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

#ifndef DIALS_SENSORS_H
#define DIALS_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum { SENSOR_MAX = 160 };

enum sensor_kind
{
  SENSOR_TEMP,			/* Degrees Celsius.  */
  SENSOR_FAN,			/* Revolutions per minute.  */
  SENSOR_VOLTAGE,		/* Volts.  */
  SENSOR_CURRENT,		/* Amperes.  */
  SENSOR_POWER			/* Watts.  */
};

struct sensor
{
  char chip[56];		/* Driver name, numbered if repeated.  */
  char label[48];
  enum sensor_kind kind;
  double value, lo, hi;		/* Now, and lowest and highest seen.  */
  double limit;			/* Critical or highest allowed, or NaN.  */
  struct metric m;

  /* Private.  */
  int fd;
  double scale;			/* Divides the raw reading.  */
};

struct sensors
{
  int n;
  struct sensor s[SENSOR_MAX];	/* By chip, then by kind and number.  */

  /* Private.  */
  bool demo;
  uint64_t rng;
};

void sensors_open (struct sensors *s, bool demo, size_t history);
void sensors_sample (struct sensors *s);
void sensors_clear_history (struct sensors *s);
void sensors_close (struct sensors *s);

/* Format the reading V of a sensor of kind K.  */
void fmt_sensor (char *buf, size_t size, enum sensor_kind k, double v);

#endif /* DIALS_SENSORS_H */

/* pages.c -- the list of pages.
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

#include "pages.h"

static const char *const network_plan[] = {
  N_("Receive and transmit rates per interface"),
  N_("Errors, drops, link speed and addresses"),
  N_("TCP connections by state"),
  NULL
};

static const char *const processes_plan[] = {
  N_("A process table you can sort and filter"),
  N_("Tree view; F8 kills and F7 renices a process"),
  NULL
};

static const char *const sensors_plan[] = {
  N_("Every temperature, fan and voltage sensor"),
  N_("Lowest and highest readings since start"),
  NULL
};

static const char *const power_plan[] = {
  N_("Battery charge, rate and time remaining"),
  N_("Processor package and memory power (RAPL)"),
  NULL
};

const struct page pages[] = {
  { "overview", N_("Overview"), page_overview_draw, NULL, NULL },
  { "cpu", N_("CPU"), page_cpu_draw, NULL, NULL },
  { "memory", N_("Memory"), page_memory_draw, NULL, NULL },
  { "gpu", N_("GPU"), page_gpu_draw, NULL, page_gpu_key },
  { "disk", N_("Disks"), page_disk_draw, NULL, page_disk_key },
  { "network", N_("Network"), page_placeholder_draw, network_plan, NULL },
  { "processes", N_("Processes"), page_placeholder_draw,
    processes_plan, NULL },
  { "sensors", N_("Sensors"), page_placeholder_draw, sensors_plan, NULL },
  { "power", N_("Power"), page_placeholder_draw, power_plan, NULL },
};

const size_t page_count = ARRAY_CARDINALITY (pages);

int
page_find (const char *id)
{
  for (size_t i = 0; i < page_count; i++)
    if (strcmp (pages[i].id, id) == 0)
      return (int) i;
  return -1;
}

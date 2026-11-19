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

#include <config.h>

#include "system.h"

#include "pages.h"

const struct page pages[] = {
  { .id = "overview", .title = N_("Overview"), .draw = page_overview_draw },
  { .id = "cpu", .title = N_("CPU"), .draw = page_cpu_draw },
  { .id = "memory", .title = N_("Memory"), .draw = page_memory_draw },
  { .id = "gpu", .title = N_("GPU"), .draw = page_gpu_draw,
    .key = page_gpu_key },
  { .id = "disk", .title = N_("Disks"), .draw = page_disk_draw,
    .key = page_disk_key },
  { .id = "network", .title = N_("Network"), .draw = page_net_draw,
    .key = page_net_key },
  { .id = "processes", .title = N_("Processes"), .draw = page_proc_draw,
    .key = page_proc_key, .fkeys = page_proc_fkeys },
  { .id = "sensors", .title = N_("Sensors"), .draw = page_sensors_draw,
    .key = page_sensors_key },
  { .id = "power", .title = N_("Power"), .draw = page_power_draw },
};

const size_t page_count = ARRAY_CARDINALITY (pages);

int
page_find (const char *id)
{
  for (size_t i = 0; i < page_count; i++)
    if (streq (pages[i].id, id))
      return (int) i;
  return -1;
}

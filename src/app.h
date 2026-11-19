/* app.h -- program state and the main loop.
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

#ifndef DIALS_APP_H
#define DIALS_APP_H

#include <stdbool.h>
#include <stddef.h>

#include "canvas.h"
#include "gpu.h"
#include "metric.h"
#include "source.h"

enum app_mode
{
  MODE_NORMAL,
  MODE_MENU,			/* A pull-down menu is open.  */
  MODE_HELP,
  MODE_ABOUT
};

struct app
{
  struct canvas cv;
  struct source *src;
  struct sys_info info;
  struct sys_sample cur;
  struct metric m_cpu, m_mem, m_swap;	/* Percentages.  */
  struct gpus gpus;
  size_t gpu_sel;		/* GPU shown in detail on the GPU page.  */
  double interval;		/* Seconds between samples.  */
  bool paused, quit, dirty, demo;
  size_t page;			/* Index into pages[].  */
  int theme;
  enum app_mode mode;
  int menu, menu_item;		/* Open menu and its selected item.  */
  int timer_fd;
};

void app_init (struct app *a, struct source *src, double interval,
               size_t page, int theme, bool demo);
void app_fini (struct app *a);

/* Run the full-screen interface.  Return the number of the signal
   that ended it, or 0.  */
int app_run (struct app *a);

void app_sample (struct app *a);
void app_set_page (struct app *a, size_t page);

/* Sample less often if DIR > 0, more often if DIR < 0.  */
void app_step_interval (struct app *a, int dir);
void app_toggle_pause (struct app *a);
void app_reset_history (struct app *a);
void app_next_theme (struct app *a);

#endif /* DIALS_APP_H */

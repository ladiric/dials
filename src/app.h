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
#include "cpu.h"
#include "disk.h"
#include "gpu.h"
#include "log.h"
#include "memory.h"
#include "metric.h"
#include "net.h"
#include "power.h"
#include "procs.h"
#include "sensors.h"
#include "settings.h"
#include "source.h"

enum app_mode
{
  MODE_NORMAL,
  MODE_MENU,			/* A pull-down menu is open.  */
  MODE_HELP,
  MODE_ABOUT,
  MODE_PROMPT,			/* A line of text is being typed.  */
  MODE_CHOICE			/* A list of choices is open.  */
};

struct app;

/* A one-line text prompt.  ACCEPT gets the text when Enter is
   pressed.  */
struct prompt
{
  char title[64];
  char label[160];
  char text[128];
  void (*accept) (struct app *a, const char *text);
};

/* A list to choose from.  ACCEPT gets the index of the choice.  */
enum { CHOICE_MAX = 12 };
struct choice
{
  char title[64];
  char label[160];
  const char *items[CHOICE_MAX];
  int n, sel;
  void (*accept) (struct app *a, int index);
};

/* How the Processes page orders the processes.  */
enum proc_sort
{
  SORT_CPU, SORT_MEM, SORT_PID, SORT_USER, SORT_NAME, SORT_TIME,
  SORT_COUNT
};

struct app
{
  struct canvas cv;
  struct source *src;
  struct sys_info info;
  struct sys_sample cur;
  struct cpu cpu;
  struct memory mem;
  struct disks disks;
  struct gpus gpus;
  struct net net;
  struct procs procs;
  struct sensors sensors;
  struct power power;
  size_t disk_sel;		/* Disk shown in detail on the Disks page.  */
  size_t gpu_sel;		/* GPU shown in detail on the GPU page.  */
  size_t net_sel;		/* Interface shown in detail.  */
  size_t sensor_sel;		/* Sensor whose history is graphed.  */
  unsigned int proc_pid;	/* Selected process, or 0.  */
  int proc_top;			/* First row shown of the process list.  */
  enum proc_sort proc_sort;
  bool proc_tree;		/* Show processes as a tree.  */
  char proc_filter[128];	/* Show only processes matching this.  */
  struct prompt prompt;
  struct choice choice;
  struct logger log;
  char log_path[4096];		/* From --log, or "".  */
  char config_path[4096];	/* The settings file, or "".  */
  char message[200];		/* A short note in the menu bar.  */
  bool message_error;
  double message_until;		/* When to stop showing it.  */
  double interval;		/* Seconds between samples.  */
  bool paused, quit, dirty, demo;
  size_t page;			/* The active panel's page, in pages[].  */
  size_t panels[2];		/* The pages of the left and right panels.  */
  int active;			/* The panel that keys go to: 0 or 1.  */
  bool split;			/* Whether both panels are shown.  */
  int theme;
  enum app_mode mode;
  int menu, menu_item;		/* Open menu and its selected item.  */
  int timer_fd;
};

void app_init (struct app *a, struct source *src, const struct settings *s,
               bool demo);
void app_fini (struct app *a);

/* Run the full-screen interface.  Return the number of the signal
   that ended it, or 0.  */
int app_run (struct app *a);

void app_sample (struct app *a);

/* Show PAGE in the active panel.  */
void app_set_page (struct app *a, size_t page);

/* Whether a page drawn by DRAW is on the screen.  */
bool app_shows (const struct app *a,
                void (*draw) (struct app *, struct canvas *, struct rect));

void app_toggle_split (struct app *a);

/* Start logging, to the --log file or a new one in the working
   directory, or stop it.  */
void app_toggle_log (struct app *a);

/* Write the current settings to the settings file.  */
void app_save_setup (struct app *a);
void app_other_panel (struct app *a);
void app_swap_panels (struct app *a);

/* Sample less often if DIR > 0, more often if DIR < 0.  */
void app_step_interval (struct app *a, int dir);
void app_toggle_pause (struct app *a);
void app_reset_history (struct app *a);
void app_next_theme (struct app *a);

/* Show a short note in the menu bar for a few seconds.  */
void app_message (struct app *a, bool error, const char *format, ...)
  __attribute__ ((__format__ (__printf__, 3, 4)));

#endif /* DIALS_APP_H */

/* settings.h -- settings kept between runs.
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

#ifndef DIALS_SETTINGS_H
#define DIALS_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

struct settings
{
  double interval;		/* Seconds between samples.  */
  int theme;
  size_t panels[2];		/* Pages of the left and right panels.  */
  int active;			/* 0 for the left panel, 1 for the right.  */
  bool split;			/* Show both panels.  */
  int sort;			/* How processes are sorted; enum proc_sort.  */
  bool tree;			/* Show processes as a tree.  */
};

void settings_init (struct settings *s);

/* Store the settings file's name in BUF: dials/dials.conf under
   $XDG_CONFIG_HOME, or under ~/.config.  Return false if there is no
   home directory to put it in.  */
bool settings_file (char *buf, size_t size);

/* Read the settings in PATH, if it exists, warning about lines that
   cannot be understood.  */
void settings_load (struct settings *s, const char *path);

/* Write the settings to PATH, making its directory if needed.  Return
   0 or an errno value.  */
int settings_save (const struct settings *s, const char *path);

#endif /* DIALS_SETTINGS_H */

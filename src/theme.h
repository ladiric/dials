/* theme.h -- named display styles and color themes.
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

#ifndef DIALS_THEME_H
#define DIALS_THEME_H

#include "tui.h"

/* Every cell on screen has one of these roles; a theme maps each role
   to colors and attributes.  */
enum style
{
  ST_BASE, ST_FRAME, ST_TITLE, ST_LABEL, ST_VALUE, ST_DIM,
  ST_OK, ST_WARN, ST_CRIT,
  ST_MENU, ST_MENU_HOT, ST_MENU_SEL, ST_MENU_SEL_HOT,
  ST_KEY, ST_KEYLBL, ST_ALERT, ST_SEL,
  ST_DLG, ST_DLG_FRAME, ST_DLG_TITLE,
  ST_COUNT
};

int theme_count (void);
const char *theme_name (int idx);

/* Return the index of the theme called NAME, or -1.  */
int theme_find (const char *name);

/* Make theme IDX current.  Call after start_color.  */
void theme_apply (int idx);

/* Store the attributes and color pair of style ST in the current
   theme.  */
void theme_style (int st, attr_t *attr, short *pair);

#endif /* DIALS_THEME_H */

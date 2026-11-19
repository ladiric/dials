/* theme.c -- named display styles and color themes.
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

#include "theme.h"

/* In a style table, BG stands for the theme's own background color.  */
#define BG (-2)
#define K COLOR_BLACK
#define R COLOR_RED
#define G COLOR_GREEN
#define Y COLOR_YELLOW
#define B COLOR_BLUE
#define C COLOR_CYAN
#define W COLOR_WHITE

struct style_def
{
  short fg, bg;
  bool bold;
};

/* White on blue with cyan frames, the look of classic text-mode
   applications.  */
static const struct style_def classic[ST_COUNT] = {
  [ST_BASE] = { W, BG, false },
  [ST_FRAME] = { C, BG, true },
  [ST_TITLE] = { Y, BG, true },
  [ST_LABEL] = { C, BG, false },
  [ST_VALUE] = { W, BG, true },
  [ST_DIM] = { C, BG, false },
  [ST_OK] = { G, BG, true },
  [ST_WARN] = { Y, BG, true },
  [ST_CRIT] = { R, BG, true },
  [ST_MENU] = { K, C, false },
  [ST_MENU_HOT] = { Y, C, true },
  [ST_MENU_SEL] = { W, K, true },
  [ST_MENU_SEL_HOT] = { Y, K, true },
  [ST_KEY] = { W, K, true },
  [ST_KEYLBL] = { K, C, false },
  [ST_ALERT] = { W, R, true },
  [ST_SEL] = { K, C, false },
  [ST_DLG] = { K, W, false },
  [ST_DLG_FRAME] = { K, W, false },
  [ST_DLG_TITLE] = { B, W, true },
};

/* For terminals without color.  */
static const attr_t mono[ST_COUNT] = {
  [ST_BASE] = A_NORMAL,
  [ST_FRAME] = A_NORMAL,
  [ST_TITLE] = A_BOLD,
  [ST_LABEL] = A_NORMAL,
  [ST_VALUE] = A_BOLD,
  [ST_DIM] = A_DIM,
  [ST_OK] = A_NORMAL,
  [ST_WARN] = A_BOLD,
  [ST_CRIT] = A_BOLD | A_UNDERLINE,
  [ST_MENU] = A_REVERSE,
  [ST_MENU_HOT] = A_REVERSE | A_BOLD,
  [ST_MENU_SEL] = A_NORMAL,
  [ST_MENU_SEL_HOT] = A_BOLD,
  [ST_KEY] = A_NORMAL,
  [ST_KEYLBL] = A_REVERSE,
  [ST_ALERT] = A_REVERSE | A_BOLD,
  [ST_SEL] = A_REVERSE,
  [ST_DLG] = A_REVERSE,
  [ST_DLG_FRAME] = A_REVERSE,
  [ST_DLG_TITLE] = A_REVERSE | A_BOLD,
};

struct theme
{
  const char *name;
  bool mono;
  short bg256;			/* Background on 256-color terminals.  */
  short bg8;			/* Background elsewhere.  */
};

static const struct theme themes[] = {
  { "classic", false, 18, COLOR_BLUE },	/* 18 is a dark navy.  */
  { "blue", false, COLOR_BLUE, COLOR_BLUE },
  { "dark", false, COLOR_BLACK, COLOR_BLACK },
  { "mono", true, 0, 0 },
};

static attr_t cur_attr[ST_COUNT];
static short cur_pair[ST_COUNT];

int
theme_count (void)
{
  return ARRAY_CARDINALITY (themes);
}

const char *
theme_name (int idx)
{
  return themes[idx].name;
}

int
theme_find (const char *name)
{
  for (int i = 0; i < theme_count (); i++)
    if (strcmp (themes[i].name, name) == 0)
      return i;
  return -1;
}

void
theme_apply (int idx)
{
  const struct theme *t = &themes[idx];

  if (t->mono || !has_colors () || COLORS < 8)
    {
      for (int i = 0; i < ST_COUNT; i++)
        {
          cur_attr[i] = mono[i];
          cur_pair[i] = 0;
        }
      return;
    }

  short bg = COLORS >= 256 ? t->bg256 : t->bg8;
  for (int i = 0; i < ST_COUNT; i++)
    {
      short fg = classic[i].fg == BG ? bg : classic[i].fg;
      short back = classic[i].bg == BG ? bg : classic[i].bg;
      init_pair (i + 1, fg, back);
      cur_pair[i] = i + 1;
      cur_attr[i] = classic[i].bold ? A_BOLD : A_NORMAL;
    }
}

void
theme_style (int st, attr_t *attr, short *pair)
{
  *attr = cur_attr[st];
  *pair = cur_pair[st];
}

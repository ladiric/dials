/* canvas.h -- an off-screen character grid that is flushed to curses.
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

#ifndef DIALS_CANVAS_H
#define DIALS_CANVAS_H

#include <stdbool.h>
#include <wchar.h>

#include "tui.h"

struct rect
{
  int y, x, h, w;
};

/* One screen cell.  CH is 0 in the right half of a double-width
   character.  ST is an enum style.  */
struct cell
{
  wchar_t ch;
  unsigned char st;
};

struct canvas
{
  int h, w;
  struct cell *cells;
  bool utf8;			/* Whether the locale can show box drawing.  */
};

void canvas_init (struct canvas *cv);
void canvas_free (struct canvas *cv);
void canvas_resize (struct canvas *cv, int h, int w);

/* All drawing is clipped to the canvas.  */
void canvas_fill (struct canvas *cv, struct rect r, int st);
void canvas_putwc (struct canvas *cv, int y, int x, wchar_t wc, int st);
void canvas_repeat (struct canvas *cv, int y, int x, int n, wchar_t wc,
                    int st);

/* Draw the multibyte string S at Y, X using at most MAXW columns
   (no limit if MAXW is negative).  Return the columns used.  */
int canvas_put (struct canvas *cv, int y, int x, int maxw, const char *s,
                int st);
int canvas_put_center (struct canvas *cv, int y, int x, int w,
                       const char *s, int st);
int canvas_put_right (struct canvas *cv, int y, int x, int w,
                      const char *s, int st);

/* Draw a frame around R with double or single lines.  */
void canvas_box (struct canvas *cv, struct rect r, int st, bool dbl);

/* Center " TITLE " on the frame line that starts at Y, X and is W
   columns wide.  */
void canvas_title (struct canvas *cv, int y, int x, int w,
                   const char *title, int st);

/* Draw a single-line divider across a double frame of width W.  */
void canvas_divider (struct canvas *cv, int y, int x, int w, int st);

void canvas_flush (const struct canvas *cv, WINDOW *win);

/* Return the number of columns the multibyte string S occupies.  */
int text_width (const char *s);

#endif /* DIALS_CANVAS_H */

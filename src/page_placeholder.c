/* page_placeholder.c -- stand-in for pages not written yet.
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

#include <stdio.h>

#include "app.h"
#include "pages.h"
#include "theme.h"

void
page_placeholder_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct page *p = &pages[a->page];
  char head[200];

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _(p->title), ST_TITLE);

  snprintf (head, sizeof head, "%s", _("This page is not available yet."));
  const char *intro = _("It will show:");

  int n = 0, tw = text_width (head);
  while (p->planned && p->planned[n])
    {
      int lw = 2 + text_width (_(p->planned[n]));
      if (lw > tw)
        tw = lw;
      n++;
    }

  int x = r.x + (r.w - tw) / 2;
  if (x < r.x + 2)
    x = r.x + 2;
  int maxw = r.x + r.w - 2 - x;
  int y = r.y + (r.h - (n + 3)) / 2;
  if (y < r.y + 1)
    y = r.y + 1;
  int last = r.y + r.h - 2;

  canvas_put (cv, y, x, maxw, head, ST_TITLE);
  if (y + 2 <= last)
    canvas_put (cv, y + 2, x, maxw, intro, ST_LABEL);
  for (int i = 0; i < n && y + 3 + i <= last; i++)
    {
      canvas_putwc (cv, y + 3 + i, x, L'•', ST_FRAME);
      canvas_put (cv, y + 3 + i, x + 2, maxw - 2, _(p->planned[i]), ST_VALUE);
    }
}

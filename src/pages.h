/* pages.h -- the screens the user switches between.
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

#ifndef DIALS_PAGES_H
#define DIALS_PAGES_H

#include <stdbool.h>
#include <stddef.h>

#include "canvas.h"

struct app;

/* A page draws itself into a rectangle, so that two pages can later
   be shown side by side in panels.  */
struct page
{
  const char *id;		/* Name for --page.  */
  const char *title;		/* Untranslated; use _().  */
  void (*draw) (struct app *a, struct canvas *cv, struct rect r);
  const char *const *planned;	/* Untranslated, NULL-terminated.  */
  /* Handle key CH if it means something on this page; return whether
     it did.  May be NULL.  */
  bool (*key) (struct app *a, int ch);
};

extern const struct page pages[];
extern const size_t page_count;

/* Return the index of the page called ID, or -1.  */
int page_find (const char *id);

void page_overview_draw (struct app *a, struct canvas *cv, struct rect r);
void page_placeholder_draw (struct app *a, struct canvas *cv,
                            struct rect r);
void page_cpu_draw (struct app *a, struct canvas *cv, struct rect r);
void page_memory_draw (struct app *a, struct canvas *cv, struct rect r);
void page_disk_draw (struct app *a, struct canvas *cv, struct rect r);
bool page_disk_key (struct app *a, int ch);
void page_gpu_draw (struct app *a, struct canvas *cv, struct rect r);
bool page_gpu_key (struct app *a, int ch);

#endif /* DIALS_PAGES_H */

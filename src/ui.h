/* ui.h -- the menu bar, function-key bar, menus and dialogs.
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

#ifndef DIALS_UI_H
#define DIALS_UI_H

struct app;

void ui_draw_menubar (struct app *a);
void ui_draw_fkeys (struct app *a);

/* Draw whatever the current mode puts on top: a menu or a dialog.  */
void ui_draw_overlay (struct app *a);

void ui_open_menu (struct app *a, int menu);

/* Handle key CH while a menu is open.  */
void ui_menu_key (struct app *a, int ch);

/* Ask for a line of text, starting with TEXT; call ACCEPT with it.  */
void ui_prompt (struct app *a, const char *title, const char *label,
                const char *text, void (*accept) (struct app *, const char *));
void ui_prompt_key (struct app *a, int ch);

/* Offer the N ITEMS, with SEL chosen at first; call ACCEPT with the
   index of the one chosen.  */
void ui_choice (struct app *a, const char *title, const char *label,
                const char *const *items, int n, int sel,
                void (*accept) (struct app *, int));
void ui_choice_key (struct app *a, int ch);

#endif /* DIALS_UI_H */

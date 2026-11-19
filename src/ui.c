/* ui.c -- the menu bar, function-key bar, menus and dialogs.
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
#include <time.h>

#include "app.h"
#include "pages.h"
#include "theme.h"
#include "ui.h"
#include "util.h"
#include "widgets.h"

enum { MENU_PAGE, MENU_OPTIONS, MENU_HELP, MENU_COUNT };
enum { MAX_ITEMS = 16 };

static const char *const menu_titles[MENU_COUNT] = {
  N_("Page"), N_("Options"), N_("Help")
};

/* Column of each menu title, as last drawn.  */
static int menu_xpos[MENU_COUNT];

struct menu_item
{
  char label[128];
  char hint[32];
  void (*act) (struct app *a, int arg);
  int arg;
};

static void
act_page (struct app *a, int arg)
{
  app_set_page (a, (size_t) arg);
}

static void
act_pause (struct app *a, int arg)
{
  (void) arg;
  app_toggle_pause (a);
}

static void
act_interval (struct app *a, int arg)
{
  app_step_interval (a, arg);
}

static void
act_reset (struct app *a, int arg)
{
  (void) arg;
  app_reset_history (a);
}

static void
act_theme (struct app *a, int arg)
{
  (void) arg;
  app_next_theme (a);
}

static void
act_mode (struct app *a, int arg)
{
  a->mode = (enum app_mode) arg;
}

static void
act_quit (struct app *a, int arg)
{
  (void) arg;
  a->quit = true;
}

static void
add_item (struct menu_item *it, const char *label, const char *hint,
          void (*act) (struct app *, int), int arg)
{
  snprintf (it->label, sizeof it->label, "%s", label);
  snprintf (it->hint, sizeof it->hint, "%s", hint);
  it->act = act;
  it->arg = arg;
}

/* Fill ITEMS with the entries of MENU in the current state and return
   how many there are.  */
static int
menu_build (struct app *a, int menu, struct menu_item *items)
{
  int n = 0;
  char buf[128];

  switch (menu)
    {
    case MENU_PAGE:
      for (size_t i = 0; i < page_count && n < MAX_ITEMS; i++)
        {
          char hint[16] = "";
          if (i < 9)
            snprintf (hint, sizeof hint, "Alt-%zu", i + 1);
          add_item (&items[n++], _(pages[i].title), hint, act_page, (int) i);
        }
      break;

    case MENU_OPTIONS:
      add_item (&items[n++], a->paused ? _("Resume") : _("Pause"), "F2",
                act_pause, 0);
      add_item (&items[n++], _("Sample less often"), "F3", act_interval, 1);
      add_item (&items[n++], _("Sample more often"), "F4", act_interval, -1);
      add_item (&items[n++], _("Clear history"), "F7", act_reset, 0);
      snprintf (buf, sizeof buf, _("Theme: %s"), theme_name (a->theme));
      add_item (&items[n++], buf, "t", act_theme, 0);
      add_item (&items[n++], _("Quit"), "F10", act_quit, 0);
      break;

    case MENU_HELP:
      add_item (&items[n++], _("Keys"), "F1", act_mode, MODE_HELP);
      add_item (&items[n++], _("About Dials"), "", act_mode, MODE_ABOUT);
      break;
    }
  return n;
}

void
ui_open_menu (struct app *a, int menu)
{
  a->mode = MODE_MENU;
  a->menu = menu;
  a->menu_item = 0;
}

void
ui_menu_key (struct app *a, int ch)
{
  struct menu_item items[MAX_ITEMS];
  int n = menu_build (a, a->menu, items);

  switch (ch)
    {
    case KEY_LEFT:
      ui_open_menu (a, (a->menu + MENU_COUNT - 1) % MENU_COUNT);
      break;
    case KEY_RIGHT:
      ui_open_menu (a, (a->menu + 1) % MENU_COUNT);
      break;
    case KEY_UP:
      a->menu_item = (a->menu_item + n - 1) % n;
      break;
    case KEY_DOWN:
      a->menu_item = (a->menu_item + 1) % n;
      break;
    case KEY_HOME:
      a->menu_item = 0;
      break;
    case KEY_END:
      a->menu_item = n - 1;
      break;
    case '\n':
    case '\r':
    case KEY_ENTER:
      a->mode = MODE_NORMAL;
      items[a->menu_item].act (a, items[a->menu_item].arg);
      break;
    case KEY_F (9):
    case KEY_F (10):
      a->mode = MODE_NORMAL;
      break;
    default:
      break;
    }
}

void
ui_draw_menubar (struct app *a)
{
  struct canvas *cv = &a->cv;
  int x = 2;

  canvas_fill (cv, (struct rect) { 0, 0, 1, cv->w }, ST_MENU);
  for (int i = 0; i < MENU_COUNT; i++)
    {
      const char *t = _(menu_titles[i]);
      bool open = a->mode == MODE_MENU && a->menu == i;
      int tw = text_width (t);
      menu_xpos[i] = x;
      canvas_fill (cv, (struct rect) { 0, x, 1, tw + 2 },
                   open ? ST_MENU_SEL : ST_MENU);
      canvas_put (cv, 0, x + 1, -1, t, open ? ST_MENU_SEL : ST_MENU);
      /* The first letter is highlighted, as in the classic interfaces.  */
      canvas_put (cv, 0, x + 1, 1, t, open ? ST_MENU_SEL_HOT : ST_MENU_HOT);
      x += tw + 3;
    }

  /* Status on the right; drop parts that do not fit.  */
  char hms[16], ival[32], full[256], shorter[128];
  time_t now = time (NULL);
  struct tm tm;
  localtime_r (&now, &tm);
  strftime (hms, sizeof hms, "%H:%M:%S", &tm);
  fmt_interval (ival, sizeof ival, a->interval);
  const char *sep = cv->utf8 ? " │ " : " | ";
  const char *state = a->paused ? _("PAUSED") : a->demo ? _("DEMO") : _("LIVE");
  snprintf (shorter, sizeof shorter, " %s%s%s%s%s ",
            state, sep, ival, sep, hms);
  snprintf (full, sizeof full, " %s%s%s", _(pages[a->page].title), sep,
            shorter + 1);

  const char *status = full;
  if (x + text_width (full) > cv->w)
    status = shorter;
  int sw = text_width (status);
  if (x + sw <= cv->w)
    canvas_put (cv, 0, cv->w - sw, sw, status, ST_MENU);
}

void
ui_draw_fkeys (struct app *a)
{
  struct canvas *cv = &a->cv;
  const char *labels[10] = {
    _("Help"), a->paused ? _("Resume") : _("Pause"), _("Slower"),
    _("Faster"), "", "", _("Reset"), "", _("Menu"), _("Quit")
  };
  int y = cv->h - 1, slot = cv->w / 10;

  for (int i = 0; i < 10; i++)
    {
      int x = i * slot;
      int wd = i < 9 ? slot : cv->w - 9 * slot;
      char key[4];
      snprintf (key, sizeof key, "%d", i + 1);
      int kw = canvas_put (cv, y, x, wd, key, ST_KEY);
      canvas_fill (cv, (struct rect) { y, x + kw, 1, wd - kw }, ST_KEYLBL);
      canvas_put (cv, y, x + kw, wd - kw, labels[i], ST_KEYLBL);
    }
}

static void
draw_pulldown (struct app *a)
{
  struct canvas *cv = &a->cv;
  struct menu_item items[MAX_ITEMS];
  int n = menu_build (a, a->menu, items);
  int lw = 0, hw = 0;

  for (int i = 0; i < n; i++)
    {
      int l = text_width (items[i].label), h = text_width (items[i].hint);
      if (l > lw)
        lw = l;
      if (h > hw)
        hw = h;
    }
  int w = lw + hw + 7;
  int x = menu_xpos[a->menu] - 1;
  if (x + w > cv->w)
    x = cv->w - w;
  if (x < 0)
    x = 0;
  struct rect r = { 1, x, n + 2, w };

  canvas_fill (cv, r, ST_MENU);
  canvas_box (cv, r, ST_MENU, true);
  for (int i = 0; i < n; i++)
    {
      int y = r.y + 1 + i;
      int st = i == a->menu_item ? ST_MENU_SEL : ST_MENU;
      canvas_fill (cv, (struct rect) { y, x + 1, 1, w - 2 }, st);
      canvas_put (cv, y, x + 2, lw, items[i].label, st);
      canvas_put_right (cv, y, x + w - 2 - hw, hw, items[i].hint, st);
    }
}

static void
draw_help (struct app *a)
{
  static const struct
  {
    const char *keys;
    const char *desc;
  } keys[] = {
    { "F1  h  ?", N_("Show this help") },
    { "F2  p  Space", N_("Pause or resume sampling") },
    { "F3  -", N_("Sample less often") },
    { "F4  +", N_("Sample more often") },
    { "F7  r", N_("Clear the history graphs") },
    { "F9", N_("Open the menu bar") },
    { "F10  q", N_("Quit") },
    { "1-9  Alt-1..9", N_("Go to a page") },
    { "Up  Down", N_("Choose a GPU on the GPU page") },
    { "Ctrl-PgUp/PgDn", N_("Previous or next page") },
    { "t", N_("Next color theme") },
  };
  enum { N = ARRAY_CARDINALITY (keys) };
  char buf[N][200];
  const char *lines[N + 2];

  for (int i = 0; i < N; i++)
    {
      snprintf (buf[i], sizeof buf[i], "%-16s%s", keys[i].keys,
                _(keys[i].desc));
      lines[i] = buf[i];
    }
  lines[N] = "";
  lines[N + 1] = _("Press any key to close.");
  widget_dialog (&a->cv, _("Keys"), lines, N + 2, false);
}

static void
draw_about (struct app *a)
{
  char name[64];
  snprintf (name, sizeof name, "%s %s", PACKAGE_NAME, VERSION);
  const char *lines[] = {
    name,
    _("Full-screen system resource monitor"),
    "",
    "Copyright (C) 2026 Richard L.",
    _("License GPLv3+: GNU GPL version 3 or later"),
    "<https://gnu.org/licenses/gpl.html>",
    _("This is free software: you are free to change and redistribute it."),
    _("There is NO WARRANTY, to the extent permitted by law."),
  };
  widget_dialog (&a->cv, _("About"), lines, ARRAY_CARDINALITY (lines), true);
}

void
ui_draw_overlay (struct app *a)
{
  switch (a->mode)
    {
    case MODE_MENU:
      draw_pulldown (a);
      break;
    case MODE_HELP:
      draw_help (a);
      break;
    case MODE_ABOUT:
      draw_about (a);
      break;
    case MODE_NORMAL:
      break;
    }
}

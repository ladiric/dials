/* app.c -- program state and the main loop.
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

#include <errno.h>
#include <error.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "pages.h"
#include "theme.h"
#include "ui.h"
#include "util.h"

/* For tigetstr.  It defines many short macros, so only this file
   includes it.  */
#if HAVE_NCURSESW_CURSES_H
# include <ncursesw/term.h>
#else
# include <term.h>
#endif

/* Samples kept per metric: an hour at the default interval.  */
enum { HISTORY = 3600 };
enum { MIN_W = 60, MIN_H = 16 };

/* Narrower than this, only the active panel is shown.  */
enum { SPLIT_MIN_W = 80 };

/* Intervals that F3 and F4 step through, in seconds.  */
static const double steps[] = { 0.25, 0.5, 1, 2, 3, 5, 10, 30, 60 };

static volatile sig_atomic_t got_signal;
static int key_ctrl_pgdn, key_ctrl_pgup;

void
app_init (struct app *a, struct source *src, const struct settings *s,
          bool demo)
{
  memset (a, 0, sizeof *a);
  a->src = src;
  a->interval = s->interval;
  a->panels[0] = s->panels[0];
  a->panels[1] = s->panels[1];
  a->active = s->active;
  a->split = s->split;
  a->page = a->panels[a->active];
  a->theme = s->theme;
  a->proc_sort = (enum proc_sort) s->sort;
  a->proc_tree = s->tree;
  a->demo = demo;
  a->timer_fd = -1;
  source_info (src, &a->info);
  sys_sample_clear (&a->cur);
  cpu_open (&a->cpu, demo, HISTORY);
  memory_open (&a->mem, demo, HISTORY);
  disks_open (&a->disks, demo, HISTORY);
  gpus_open (&a->gpus, demo, HISTORY);
  net_open (&a->net, demo, HISTORY);
  procs_open (&a->procs, demo);
  sensors_open (&a->sensors, demo, HISTORY);
  power_open (&a->power, demo, HISTORY);
}

void
app_fini (struct app *a)
{
  if (a->timer_fd >= 0)
    close (a->timer_fd);
  canvas_free (&a->cv);
  cpu_close (&a->cpu);
  memory_close (&a->mem);
  disks_close (&a->disks);
  log_stop (&a->log);
  gpus_close (&a->gpus);
  net_close (&a->net);
  procs_close (&a->procs);
  sensors_close (&a->sensors);
  power_close (&a->power);
}

void
app_sample (struct app *a)
{
  source_sample (a->src, &a->cur);
  cpu_sample (&a->cpu);
  /* Scanning processes and file systems costs more, so it is done only
     while they are shown.  */
  memory_sample (&a->mem, app_shows (a, page_memory_draw));
  disks_sample (&a->disks, app_shows (a, page_disk_draw));
  gpus_sample (&a->gpus, app_shows (a, page_gpu_draw));
  net_sample (&a->net, app_shows (a, page_net_draw));
  if (app_shows (a, page_proc_draw))
    procs_sample (&a->procs);
  sensors_sample (&a->sensors);
  power_sample (&a->power);
  if (a->log.f)
    {
      int err = log_write (&a->log, a);
      if (err)
        {
          app_message (a, true, _("Stopped logging: %s: %s"), a->log.path,
                       strerror (err));
          log_stop (&a->log);
        }
    }
  a->dirty = true;
}

static struct timespec
to_timespec (double seconds)
{
  struct timespec ts;
  ts.tv_sec = (time_t) seconds;
  ts.tv_nsec = (long) ((seconds - (double) ts.tv_sec) * 1e9);
  if (ts.tv_sec == 0 && ts.tv_nsec == 0)
    ts.tv_nsec = 1;		/* Zero would disarm the timer.  */
  return ts;
}

/* Fire the sampling timer after FIRST seconds, then every interval.  */
static void
arm_timer (struct app *a, double first)
{
  struct itimerspec its;
  its.it_interval = to_timespec (a->interval);
  its.it_value = to_timespec (first);
  timerfd_settime (a->timer_fd, 0, &its, NULL);
}

bool
app_shows (const struct app *a,
           void (*draw) (struct app *, struct canvas *, struct rect))
{
  return pages[a->panels[a->active]].draw == draw
    || (a->split && pages[a->panels[!a->active]].draw == draw);
}

void
app_set_page (struct app *a, size_t page)
{
  if (page < page_count && page != a->page)
    {
      bool had_procs = app_shows (a, page_proc_draw);
      a->page = a->panels[a->active] = page;
      /* Show the processes at once instead of after the next sample.  */
      if (!had_procs && app_shows (a, page_proc_draw))
        procs_sample (&a->procs);
    }
  a->dirty = true;
}

void
app_toggle_split (struct app *a)
{
  bool had_procs = app_shows (a, page_proc_draw);
  a->split = !a->split;
  if (!had_procs && app_shows (a, page_proc_draw))
    procs_sample (&a->procs);
  a->dirty = true;
}

void
app_toggle_log (struct app *a)
{
  if (a->log.f)
    {
      app_message (a, false, _("Stopped logging to %s."), a->log.path);
      log_stop (&a->log);
      return;
    }
  char path[4096];
  if (a->log_path[0])
    snprintf (path, sizeof path, "%s", a->log_path);
  else
    {
      time_t now = time (NULL);
      struct tm tm;
      localtime_r (&now, &tm);
      strftime (path, sizeof path, "dials-%Y%m%d-%H%M%S.csv", &tm);
    }
  int err = log_start (&a->log, path, a);
  if (err)
    app_message (a, true, _("Cannot log to %s: %s"), path, strerror (err));
  else
    app_message (a, false, _("Logging to %s."), path);
}

void
app_save_setup (struct app *a)
{
  if (!a->config_path[0])
    {
      app_message (a, true, "%s",
                   _("There is no home directory to save settings in."));
      return;
    }
  struct settings s = {
    .interval = a->interval, .theme = a->theme,
    .panels = { a->panels[0], a->panels[1] }, .active = a->active,
    .split = a->split, .sort = a->proc_sort, .tree = a->proc_tree
  };
  int err = settings_save (&s, a->config_path);
  if (err)
    app_message (a, true, _("Cannot save settings in %s: %s"),
                 a->config_path, strerror (err));
  else
    app_message (a, false, _("Settings saved in %s."), a->config_path);
}

void
app_other_panel (struct app *a)
{
  if (a->split)
    {
      a->active = !a->active;
      a->page = a->panels[a->active];
    }
  a->dirty = true;
}

void
app_swap_panels (struct app *a)
{
  size_t t = a->panels[0];
  a->panels[0] = a->panels[1];
  a->panels[1] = t;
  a->page = a->panels[a->active];
  a->dirty = true;
}

void
app_message (struct app *a, bool error, const char *format, ...)
{
  va_list ap;
  va_start (ap, format);
  vsnprintf (a->message, sizeof a->message, format, ap);
  va_end (ap);
  a->message_error = error;
  a->message_until = now_mono () + 4;
  a->dirty = true;
}

void
app_step_interval (struct app *a, int dir)
{
  double cur = a->interval, next = cur;
  size_t n = ARRAY_CARDINALITY (steps);

  if (dir > 0)
    {
      for (size_t i = 0; i < n; i++)
        if (steps[i] > cur + 1e-9)
          {
            next = steps[i];
            break;
          }
    }
  else
    {
      for (size_t i = n; i-- > 0;)
        if (steps[i] < cur - 1e-9)
          {
            next = steps[i];
            break;
          }
    }
  if (next != cur)
    {
      a->interval = next;
      if (a->timer_fd >= 0)
        arm_timer (a, next);
    }
  a->dirty = true;
}

void
app_toggle_pause (struct app *a)
{
  a->paused = !a->paused;
  a->dirty = true;
}

void
app_reset_history (struct app *a)
{
  cpu_clear_history (&a->cpu);
  memory_clear_history (&a->mem);
  disks_clear_history (&a->disks);
  gpus_clear_history (&a->gpus);
  net_clear_history (&a->net);
  sensors_clear_history (&a->sensors);
  power_clear_history (&a->power);
  a->dirty = true;
}

void
app_next_theme (struct app *a)
{
  a->theme = (a->theme + 1) % theme_count ();
  theme_apply (a->theme);
  a->dirty = true;
}

static void
render (struct app *a)
{
  struct canvas *cv = &a->cv;
  int h, w;

  getmaxyx (stdscr, h, w);
  canvas_resize (cv, h, w);
  canvas_fill (cv, (struct rect) { 0, 0, h, w }, ST_BASE);
  if (w < MIN_W || h < MIN_H)
    {
      char msg[128];
      snprintf (msg, sizeof msg, _("Terminal too small: %dx%d, need %dx%d"),
                w, h, MIN_W, MIN_H);
      canvas_put_center (cv, h / 2, 0, w, msg, ST_WARN);
    }
  else if (!a->split || w < SPLIT_MIN_W)
    pages[a->page].draw (a, cv, (struct rect) { 1, 0, h - 2, w });
  else
    {
      /* Two panels.  The active one's title stands out, as the panel
         that keys go to.  */
      struct rect r[2] = { { 1, 0, h - 2, w / 2 },
                           { 1, w / 2, h - 2, w - w / 2 } };
      for (int i = 0; i < 2; i++)
        pages[a->panels[i]].draw (a, cv, r[i]);
      const struct rect *ar = &r[a->active];
      canvas_title (cv, ar->y, ar->x, ar->w, _(pages[a->page].title), ST_SEL);
    }
  ui_draw_menubar (a);
  ui_draw_fkeys (a);
  ui_draw_overlay (a);
  canvas_flush (cv, stdscr);
  wnoutrefresh (stdscr);
  doupdate ();
}

static void
handle_key (struct app *a, int ch)
{
  a->dirty = true;
  if (ch == KEY_RESIZE)
    {
      clearok (curscr, TRUE);
      return;
    }

  /* Escape alone closes menus and dialogs; Escape followed at once by
     another key is how terminals send Alt with that key.  */
  if (ch == 27)
    {
      int next = getch ();
      if (next == ERR)
        a->mode = MODE_NORMAL;
      else if (a->mode == MODE_NORMAL && next >= '1' && next <= '9')
        app_set_page (a, (size_t) (next - '1'));
      return;
    }

  switch (a->mode)
    {
    case MODE_HELP:
    case MODE_ABOUT:
      a->mode = MODE_NORMAL;
      return;
    case MODE_MENU:
      ui_menu_key (a, ch);
      return;
    case MODE_PROMPT:
      ui_prompt_key (a, ch);
      return;
    case MODE_CHOICE:
      ui_choice_key (a, ch);
      return;
    case MODE_NORMAL:
      break;
    }

  if (pages[a->page].key && pages[a->page].key (a, ch))
    return;
  if (key_ctrl_pgdn > 0 && ch == key_ctrl_pgdn)
    {
      app_set_page (a, (a->page + 1) % page_count);
      return;
    }
  if (key_ctrl_pgup > 0 && ch == key_ctrl_pgup)
    {
      app_set_page (a, (a->page + page_count - 1) % page_count);
      return;
    }

  switch (ch)
    {
    case KEY_F (1):
    case 'h':
    case '?':
      a->mode = MODE_HELP;
      break;
    case KEY_F (2):
    case 'p':
    case ' ':
      app_toggle_pause (a);
      break;
    case KEY_F (3):
    case '-':
      app_step_interval (a, 1);
      break;
    case KEY_F (4):
    case '+':
    case '=':
      app_step_interval (a, -1);
      break;
    case KEY_F (7):
    case 'r':
      app_reset_history (a);
      break;
    case KEY_F (9):
      ui_open_menu (a, 0);
      break;
    case KEY_F (10):
    case 'q':
    case 'Q':
      a->quit = true;
      break;
    case 't':
      app_next_theme (a);
      break;
    case 's':
      app_toggle_split (a);
      break;
    case 'l':
      app_toggle_log (a);
      break;
    case '\t':
      app_other_panel (a);
      break;
    case 21:			/* Ctrl-U */
      app_swap_panels (a);
      break;
    default:
      if (ch >= '1' && ch <= '9')
        app_set_page (a, (size_t) (ch - '1'));
      break;
    }
}

/* Return the key code curses gives the terminfo capability CAP, such
   as "kNXT5" for Ctrl-PgDn, or 0 if the terminal has no such key.  */
static int
extended_key (const char *cap)
{
  const char *s = tigetstr (cap);
  if (s == NULL || s == (const char *) -1)
    return 0;
  return key_defined (s);
}

static void
on_signal (int sig)
{
  got_signal = sig;
}

static void
install_signals (void)
{
  static const int sigs[] = { SIGINT, SIGTERM, SIGHUP };
  struct sigaction sa;

  memset (&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigemptyset (&sa.sa_mask);
  /* No SA_RESTART: a signal must interrupt poll.  */
  for (size_t i = 0; i < ARRAY_CARDINALITY (sigs); i++)
    sigaction (sigs[i], &sa, NULL);
}

int
app_run (struct app *a)
{
  a->timer_fd = timerfd_create (CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (a->timer_fd < 0)
    error (EXIT_FAILURE, errno, "%s", _("cannot create timer"));

  initscr ();
  cbreak ();
  noecho ();
  nonl ();
  intrflush (stdscr, FALSE);
  keypad (stdscr, TRUE);
  nodelay (stdscr, TRUE);
  curs_set (0);
  set_escdelay (25);
  if (has_colors ())
    start_color ();
  theme_apply (a->theme);
  canvas_init (&a->cv);
  key_ctrl_pgdn = extended_key ("kNXT5");
  key_ctrl_pgup = extended_key ("kPRV5");
  install_signals ();

  /* The first sample comes quickly; CPU use needs two readings.  */
  arm_timer (a, 0.2);
  a->dirty = true;
  time_t last_sec = 0;

  while (!a->quit && !got_signal)
    {
      if (a->dirty)
        {
          render (a);
          a->dirty = false;
        }

      /* Wake at the next whole second to update the clock.  */
      struct timespec ts;
      clock_gettime (CLOCK_REALTIME, &ts);
      int timeout = 1000 - (int) (ts.tv_nsec / 1000000);
      struct pollfd fds[2] = {
        { STDIN_FILENO, POLLIN, 0 },
        { a->timer_fd, POLLIN, 0 },
      };
      int n = poll (fds, 2, timeout);
      if (n < 0 && errno != EINTR)
        break;
      if (n > 0 && (fds[1].revents & POLLIN))
        {
          uint64_t expirations;
          if (read (a->timer_fd, &expirations, sizeof expirations) > 0
              && !a->paused)
            app_sample (a);
        }

      /* Also picks up KEY_RESIZE after SIGWINCH interrupts poll.  */
      int ch;
      while ((ch = getch ()) != ERR)
        handle_key (a, ch);

      time_t now = time (NULL);
      if (now != last_sec)
        {
          last_sec = now;
          a->dirty = true;
        }
    }

  endwin ();
  return got_signal;
}

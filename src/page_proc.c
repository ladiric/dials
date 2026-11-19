/* page_proc.c -- the Processes page.
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
#include <signal.h>
#include <stdio.h>
#include <strings.h>

#include "app.h"
#include "pages.h"
#include "procs.h"
#include "theme.h"
#include "tui.h"
#include "ui.h"
#include "util.h"
#include "widgets.h"

const char *const page_proc_fkeys[10] = {
  NULL, NULL, NULL, NULL, N_("Sort"), N_("Tree"), N_("Nice"), N_("Kill"),
  NULL, NULL
};

static const char *const sort_names[SORT_COUNT] = {
  N_("CPU"), N_("memory"), N_("PID"), N_("user"), N_("name"), N_("time")
};

/* A row of the list: a process and, in the tree, the lines that lead
   to it.  */
struct row
{
  int idx;			/* Into the procs list.  */
  char prefix[96];
};

/* The rows as last built, for the keys to move through.  */
static struct row *view;
static int nview, view_cap;
static int shown_rows = 10;
static int last_sel;

/* The process a dialog is about.  */
static unsigned int target_pid;
static char target_name[32];

/* Used by the comparison function.  */
static const struct proc_info *sort_list;
static enum proc_sort sort_key;

static double
or_less (double v)
{
  return isnan (v) ? -1 : v;
}

static int
compare (const void *a, const void *b)
{
  const struct proc_info *x = &sort_list[*(const int *) a];
  const struct proc_info *y = &sort_list[*(const int *) b];
  int c = 0;
  switch (sort_key)
    {
    case SORT_CPU:
      c = (or_less (y->cpu) > or_less (x->cpu)) - (or_less (y->cpu) < or_less (x->cpu));
      break;
    case SORT_MEM:
      c = (y->rss > x->rss) - (y->rss < x->rss);
      break;
    case SORT_USER:
      c = strcmp (x->user, y->user);
      break;
    case SORT_NAME:
      c = strcasecmp (x->comm, y->comm);
      break;
    case SORT_TIME:
      c = (y->time > x->time) - (y->time < x->time);
      break;
    case SORT_PID:
    case SORT_COUNT:
      break;
    }
  return c ? c : (x->pid > y->pid) - (x->pid < y->pid);
}

static bool
matches (const struct proc_info *p, const char *filter)
{
  return !filter[0] || strcasestr (p->comm, filter)
    || strcasestr (p->cmd, filter);
}

static void
push_row (int idx, const char *prefix)
{
  struct row *r = &view[nview++];
  r->idx = idx;
  snprintf (r->prefix, sizeof r->prefix, "%s", prefix);
}

/* Add process I and, below it, its children, drawing the tree's lines
   from LEAD, which shows which ancestors have more children to come.  */
static void
add_subtree (int i, const int *child, const int *sibling, char *lead,
             size_t lead_len, bool last, int depth)
{
  char prefix[96];
  if (depth == 0)
    prefix[0] = '\0';
  else
    snprintf (prefix, sizeof prefix, "%s%s", lead, last ? "└─ " : "├─ ");
  push_row (i, prefix);

  size_t len = strlen (lead);
  if (depth > 0 && len + 8 < lead_len)
    strcat (lead, last ? "   " : "│  ");
  for (int c = child[i]; c >= 0; c = sibling[c])
    add_subtree (c, child, sibling, lead, lead_len, sibling[c] < 0,
                 depth + 1);
  lead[len] = '\0';
}

static void
build_view (struct app *a)
{
  const struct procs *s = &a->procs;
  int n = s->n;
  if (view_cap < n)
    {
      free (view);
      view_cap = n + 64;
      view = xmalloc (view_cap * sizeof *view);
    }
  nview = 0;

  int *idx = xmalloc ((n + 1) * sizeof *idx);
  int m = 0;
  for (int i = 0; i < n; i++)
    if (matches (&s->p[i], a->proc_filter))
      idx[m++] = i;
  sort_list = s->p;
  sort_key = a->proc_sort;
  qsort (idx, m, sizeof *idx, compare);

  if (!a->proc_tree)
    for (int k = 0; k < m; k++)
      push_row (idx[k], "");
  else
    {
      /* Children lists in the sorted order: go through the processes
         backwards and put each at the front of its parent's list.  */
      int *child = xmalloc ((n + 1) * sizeof *child);
      int *sibling = xmalloc ((n + 1) * sizeof *sibling);
      bool *shown = xcalloc (n + 1, sizeof *shown);
      int roots = -1;
      for (int i = 0; i < n; i++)
        child[i] = sibling[i] = -1;
      for (int k = 0; k < m; k++)
        shown[idx[k]] = true;
      for (int k = m - 1; k >= 0; k--)
        {
          int i = idx[k];
          const struct proc_info *parent = procs_find (s, s->p[i].ppid);
          int pi = parent ? (int) (parent - s->p) : -1;
          if (pi >= 0 && shown[pi] && pi != i)
            {
              sibling[i] = child[pi];
              child[pi] = i;
            }
          else
            {
              sibling[i] = roots;
              roots = i;
            }
        }
      char lead[96] = "";
      for (int r = roots; r >= 0; r = sibling[r])
        add_subtree (r, child, sibling, lead, sizeof lead, true, 0);
      free (child);
      free (sibling);
      free (shown);
    }
  free (idx);
}

/* Return the row of the selected process, choosing one if it is gone.  */
static int
selected_row (struct app *a)
{
  for (int r = 0; r < nview; r++)
    if (a->procs.p[view[r].idx].pid == a->proc_pid)
      return last_sel = r;
  if (nview == 0)
    return -1;
  if (last_sel >= nview)
    last_sel = nview - 1;
  a->proc_pid = a->procs.p[view[last_sel].idx].pid;
  return last_sel;
}

static void
fmt_cputime (char *buf, size_t size, double seconds)
{
  long s = (long) seconds;
  if (s >= 3600)
    snprintf (buf, size, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
  else
    snprintf (buf, size, "%ld:%02ld", s / 60, s % 60);
}

static void
draw_summary (struct app *a, struct canvas *cv, int y, int x, int end)
{
  const struct procs *s = &a->procs;
  char b[64];
  int cx = x;
  snprintf (b, sizeof b, "%d", s->n);
  cx = widget_pair (cv, y, cx, end, _("Tasks"), b);
  snprintf (b, sizeof b, "%d", s->running);
  cx = widget_pair (cv, y, cx, end, _("running"), b);
  snprintf (b, sizeof b, "%d", s->sleeping);
  cx = widget_pair (cv, y, cx, end, _("sleeping"), b);
  snprintf (b, sizeof b, "%d", s->waiting);
  cx = widget_pair (cv, y, cx, end, _("waiting"), b);
  snprintf (b, sizeof b, "%d", s->threads);
  cx = widget_pair (cv, y, cx, end, _("Threads"), b);
  cx = widget_pair (cv, y, cx, end, _("Sorted by"), _(sort_names[a->proc_sort]));
  if (a->proc_tree)
    cx = widget_pair (cv, y, cx, end, _("Tree"), _("on"));
  if (a->proc_filter[0])
    widget_pair (cv, y, cx, end, _("Filter"), a->proc_filter);
}

void
page_proc_draw (struct app *a, struct canvas *cv, struct rect r)
{
  const struct procs *s = &a->procs;

  canvas_box (cv, r, ST_FRAME, true);
  canvas_title (cv, r.y, r.x, r.w, _("Processes"), ST_TITLE);
  int x = r.x + 2, w = r.w - 4, y = r.y + 1;
  int bottom = r.y + r.h - 1;

  if (!s->valid)
    {
      canvas_put_center (cv, y + 1, x, w, _("Reading processes..."), ST_DIM);
      return;
    }
  draw_summary (a, cv, y, x, x + w);
  y++;

  const struct table_col cols[] = {
    { _("PID"), 7, true, 0 },
    { _("User"), 9, false, 2 },
    { _("S"), 1, false, 4 },
    { _("Nice"), 4, true, 4 },
    { _("Thr"), 4, true, 5 },
    { _("CPU"), 6, true, 0 },
    { _("Mem"), 5, true, 1 },
    { _("Memory"), 9, true, 0 },
    { _("Time"), 9, true, 3 },
    { _("Command"), 0, false, 0 },
  };
  enum { N = ARRAY_CARDINALITY (cols) };
  int widths[N];
  const char *titles[N];

  table_layout (cols, N, w, widths);
  for (int i = 0; i < N; i++)
    titles[i] = cols[i].title;
  table_row (cv, y, x, w, cols, N, widths, titles, NULL, ST_TITLE);
  y++;

  build_view (a);
  int rows = bottom - y;
  shown_rows = rows > 1 ? rows : 1;
  int sel = selected_row (a);
  if (nview == 0)
    {
      canvas_put_center (cv, y + 1, x, w, _("No process matches the filter."),
                         ST_DIM);
      return;
    }
  if (sel < a->proc_top)
    a->proc_top = sel;
  if (sel >= a->proc_top + rows)
    a->proc_top = sel - rows + 1;
  if (a->proc_top > nview - rows)
    a->proc_top = nview - rows > 0 ? nview - rows : 0;

  for (int k = 0; k < rows && a->proc_top + k < nview; k++)
    {
      int ri = a->proc_top + k;
      const struct proc_info *p = &s->p[view[ri].idx];
      char pid[16], st[4], nice[16], thr[16], cpu[16], memp[16], rss[32];
      char tm[32], cmd[400];
      snprintf (pid, sizeof pid, "%u", p->pid);
      snprintf (st, sizeof st, "%c", p->state);
      snprintf (nice, sizeof nice, "%d", p->nice);
      snprintf (thr, sizeof thr, "%d", p->threads);
      if (isnan (p->cpu))
        cpu[0] = '\0';
      else
        snprintf (cpu, sizeof cpu, "%.1f", p->cpu);
      if (a->mem.total > 0)
        snprintf (memp, sizeof memp, "%.1f", 100 * p->rss / a->mem.total);
      else
        memp[0] = '\0';
      fmt_mib (rss, sizeof rss, p->rss);
      fmt_cputime (tm, sizeof tm, p->time);
      snprintf (cmd, sizeof cmd, "%s%s", view[ri].prefix, p->cmd);

      const char *cells[N] = { pid, p->user, st, nice, thr, cpu, memp, rss,
                               tm, cmd };
      int styles[N] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
      bool is_sel = ri == sel;
      if (!is_sel)
        {
          if (!isnan (p->cpu) && p->cpu >= 1)
            styles[5] = p->cpu >= 90 ? ST_CRIT : p->cpu >= 30 ? ST_WARN
              : ST_OK;
          if (p->state == 'R')
            styles[2] = ST_OK;
          else if (p->state == 'D' || p->state == 'Z')
            styles[2] = ST_CRIT;
          if (p->cmd[0] == '[')
            styles[9] = ST_DIM;	/* Kernel threads.  */
        }
      table_row (cv, y + k, x, w, cols, N, widths, cells, styles,
                 is_sel ? ST_SEL : ST_BASE);
    }
}

/* What the result of acting on a process means to the user.  */
static void
report_result (struct app *a, int err, const char *done)
{
  if (err == 0)
    app_message (a, false, "%s", done);
  else if (err == -1)
    app_message (a, false, "%s",
                 _("Simulated data: nothing was sent to any process."));
  else if (err == ENOTSUP)
    app_message (a, true, "%s",
                 _("Processes are not acted on when reading a copy of /proc."));
  else if (err == ESRCH)
    app_message (a, true, _("Process %u has already ended."), target_pid);
  else if (err == EPERM || err == EACCES)
    app_message (a, true,
                 _("Not permitted for process %u (%s); it may need root."),
                 target_pid, target_name);
  else
    app_message (a, true, "%s", strerror (err));
}

static const int signals[] = { SIGTERM, SIGKILL, SIGHUP, SIGINT, SIGSTOP,
                               SIGCONT };
static const char *const signal_names[] = { "TERM", "KILL", "HUP", "INT",
                                            "STOP", "CONT" };

static void
send_signal (struct app *a, int choice)
{
  char done[160];
  snprintf (done, sizeof done, _("Sent %s to %u (%s)."),
            signal_names[choice], target_pid, target_name);
  report_result (a, procs_signal (&a->procs, target_pid, signals[choice]),
                 done);
}

static void
set_nice (struct app *a, const char *text)
{
  char *end;
  long v = strtol (text, &end, 10);
  if (end == text || *end || v < -20 || v > 19)
    {
      app_message (a, true, _("'%s' is not a nice value from -20 to 19."),
                   text);
      return;
    }
  char done[160];
  snprintf (done, sizeof done, _("Nice value of %u (%s) is now %ld."),
            target_pid, target_name, v);
  report_result (a, procs_renice (&a->procs, target_pid, (int) v), done);
}

static void
set_filter (struct app *a, const char *text)
{
  snprintf (a->proc_filter, sizeof a->proc_filter, "%s", text);
  a->proc_top = 0;
}

/* Remember the selected process for a dialog; return false if there is
   none.  */
static bool
take_target (struct app *a)
{
  const struct proc_info *p = procs_find (&a->procs, a->proc_pid);
  if (!p)
    return false;
  target_pid = p->pid;
  snprintf (target_name, sizeof target_name, "%s", p->comm);
  return true;
}

static void
move_selection (struct app *a, int by)
{
  if (nview == 0)
    return;
  int r = last_sel + by;
  if (r < 0)
    r = 0;
  if (r >= nview)
    r = nview - 1;
  last_sel = r;
  a->proc_pid = a->procs.p[view[r].idx].pid;
}

bool
page_proc_key (struct app *a, int ch)
{
  char label[200];
  switch (ch)
    {
    case KEY_UP:
      move_selection (a, -1);
      return true;
    case KEY_DOWN:
      move_selection (a, 1);
      return true;
    case KEY_PPAGE:
      move_selection (a, -shown_rows);
      return true;
    case KEY_NPAGE:
      move_selection (a, shown_rows);
      return true;
    case KEY_HOME:
      move_selection (a, -nview);
      return true;
    case KEY_END:
      move_selection (a, nview);
      return true;
    case KEY_F (5):
      a->proc_sort = (a->proc_sort + 1) % SORT_COUNT;
      return true;
    case KEY_F (6):
      a->proc_tree = !a->proc_tree;
      return true;
    case '/':
      ui_prompt (a, _("Filter"),
                 _("Show processes whose name or command contains:"),
                 a->proc_filter, set_filter);
      return true;
    case KEY_F (7):
      if (take_target (a))
        {
          const struct proc_info *p = procs_find (&a->procs, target_pid);
          char now[16];
          snprintf (now, sizeof now, "%d", p->nice);
          snprintf (label, sizeof label,
                    _("Nice value for %u (%s), from -20 to 19:"),
                    target_pid, target_name);
          ui_prompt (a, _("Nice"), label, now, set_nice);
        }
      return true;
    case KEY_F (8):
      if (take_target (a))
        {
          const char *items[] = {
            _("TERM  ask it to end"),
            _("KILL  end it at once"),
            _("HUP   hang up, or reload its settings"),
            _("INT   interrupt it, as Ctrl-C does"),
            _("STOP  pause it"),
            _("CONT  let it continue"),
          };
          snprintf (label, sizeof label, _("Send a signal to %u (%s):"),
                    target_pid, target_name);
          ui_choice (a, _("Signal"), label, items, ARRAY_CARDINALITY (items),
                     0, send_signal);
        }
      return true;
    default:
      return false;
    }
}

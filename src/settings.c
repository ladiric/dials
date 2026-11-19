/* settings.c -- settings kept between runs.
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
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pages.h"
#include "settings.h"
#include "theme.h"
#include "util.h"

/* The file holds lines of "NAME = VALUE"; blank lines and lines
   starting with '#' are ignored.  */

static const char *const sort_ids[] = {
  "cpu", "memory", "pid", "user", "name", "time"
};

static const char *const panel_ids[] = { "left", "right" };

void
settings_init (struct settings *s)
{
  s->interval = 1;
  s->theme = 0;
  s->panels[0] = 0;
  s->panels[1] = 1;
  s->active = 0;
  s->split = false;
  s->sort = 0;
  s->tree = false;
}

bool
settings_file (char *buf, size_t size)
{
  const char *xdg = getenv ("XDG_CONFIG_HOME");
  const char *home = getenv ("HOME");
  if (xdg && xdg[0] == '/')
    snprintf (buf, size, "%s/dials/dials.conf", xdg);
  else if (home && home[0])
    snprintf (buf, size, "%s/.config/dials/dials.conf", home);
  else
    return false;
  return true;
}

/* Return the index of NAME in the N strings of LIST, or -1.  */
static int
lookup (const char *const *list, int n, const char *name)
{
  for (int i = 0; i < n; i++)
    if (strcmp (list[i], name) == 0)
      return i;
  return -1;
}

static int
yes_no (const char *v)
{
  return strcmp (v, "yes") == 0 ? 1 : strcmp (v, "no") == 0 ? 0 : -1;
}

/* Remove blanks at both ends of S.  */
static char *
strip (char *s)
{
  s += strspn (s, " \t");
  size_t n = strlen (s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'
                   || s[n - 1] == '\r'))
    s[--n] = '\0';
  return s;
}

/* Apply NAME = VALUE.  Return false if either is not understood.  */
static bool
apply (struct settings *s, const char *name, const char *value)
{
  int k;
  if (strcmp (name, "interval") == 0)
    {
      char *end;
      double v = c_strtod (value, &end);
      if (end == value || *end || !(v >= 0.1 && v <= 3600))
        return false;
      s->interval = v;
    }
  else if (strcmp (name, "theme") == 0)
    {
      if ((k = theme_find (value)) < 0)
        return false;
      s->theme = k;
    }
  else if (strcmp (name, "left-page") == 0 || strcmp (name, "right-page") == 0)
    {
      if ((k = page_find (value)) < 0)
        return false;
      s->panels[name[0] == 'r'] = (size_t) k;
    }
  else if (strcmp (name, "active-panel") == 0)
    {
      if ((k = lookup (panel_ids, 2, value)) < 0)
        return false;
      s->active = k;
    }
  else if (strcmp (name, "two-panels") == 0)
    {
      if ((k = yes_no (value)) < 0)
        return false;
      s->split = k;
    }
  else if (strcmp (name, "process-sort") == 0)
    {
      if ((k = lookup (sort_ids, ARRAY_CARDINALITY (sort_ids), value)) < 0)
        return false;
      s->sort = k;
    }
  else if (strcmp (name, "process-tree") == 0)
    {
      if ((k = yes_no (value)) < 0)
        return false;
      s->tree = k;
    }
  else
    return false;
  return true;
}

void
settings_load (struct settings *s, const char *path)
{
  FILE *f = fopen (path, "re");
  if (!f)
    {
      if (errno != ENOENT)
        error (0, errno, _("cannot read %s"), path);
      return;
    }
  char line[512];
  int number = 0;
  while (fgets (line, sizeof line, f))
    {
      number++;
      char *t = strip (line);
      if (!t[0] || t[0] == '#')
        continue;
      char *eq = strchr (t, '=');
      if (eq)
        *eq = '\0';
      if (!eq || !apply (s, strip (t), strip (eq + 1)))
        error_at_line (0, 0, path, number, "%s",
                       _("setting not understood; ignored"));
    }
  fclose (f);
}

/* Make the directory holding the file PATH, and its parents.  */
static int
make_parents (const char *path)
{
  char dir[4096];
  snprintf (dir, sizeof dir, "%s", path);
  char *slash = strrchr (dir, '/');
  if (!slash || slash == dir)
    return 0;
  *slash = '\0';
  for (char *p = dir + 1; *p; p++)
    if (*p == '/')
      {
        *p = '\0';
        if (mkdir (dir, 0777) != 0 && errno != EEXIST)
          return errno;
        *p = '/';
      }
  if (mkdir (dir, 0777) != 0 && errno != EEXIST)
    return errno;
  return 0;
}

int
settings_save (const struct settings *s, const char *path)
{
  int err = make_parents (path);
  if (err)
    return err;

  /* Write a new file and rename it over the old one, so that a failure
     cannot leave half a file.  */
  char tmp[4200];
  snprintf (tmp, sizeof tmp, "%s.new", path);
  FILE *f = fopen (tmp, "we");
  if (!f)
    return errno;
  fputs (_("# Dials settings, written by Options > Save setup.\n"), f);
  c_fprintf (f, "interval = %g\n", s->interval);
  c_fprintf (f, "theme = %s\n", theme_name (s->theme));
  c_fprintf (f, "left-page = %s\n", pages[s->panels[0]].id);
  c_fprintf (f, "right-page = %s\n", pages[s->panels[1]].id);
  c_fprintf (f, "active-panel = %s\n", panel_ids[s->active]);
  c_fprintf (f, "two-panels = %s\n", s->split ? "yes" : "no");
  c_fprintf (f, "process-sort = %s\n", sort_ids[s->sort]);
  c_fprintf (f, "process-tree = %s\n", s->tree ? "yes" : "no");
  bool failed = ferror (f) != 0;
  err = errno;
  if (fclose (f) != 0)
    {
      failed = true;
      err = errno;
    }
  if (failed)
    {
      unlink (tmp);
      return err ? err : EIO;
    }
  if (rename (tmp, path) != 0)
    {
      err = errno;
      unlink (tmp);
      return err;
    }
  return 0;
}

/* canvas.c -- an off-screen character grid that is flushed to curses.
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

#include <limits.h>
#include <langinfo.h>
#include <wchar.h>

#include "canvas.h"
#include "theme.h"
#include "util.h"

/* Pages are drawn on the canvas from scratch on every refresh; curses
   then sends only the cells that changed to the terminal.  */

void
canvas_init (struct canvas *cv)
{
  cv->h = cv->w = 0;
  cv->cells = NULL;
  cv->utf8 = strcmp (nl_langinfo (CODESET), "UTF-8") == 0;
}

void
canvas_free (struct canvas *cv)
{
  free (cv->cells);
  cv->cells = NULL;
  cv->h = cv->w = 0;
}

void
canvas_resize (struct canvas *cv, int h, int w)
{
  if (h < 0)
    h = 0;
  if (w < 0)
    w = 0;
  if (h == cv->h && w == cv->w && cv->cells)
    return;
  free (cv->cells);
  cv->cells = xcalloc ((size_t) h * w + 1, sizeof *cv->cells);
  cv->h = h;
  cv->w = w;
}

/* Store WC, which is CW columns wide, at Y, X.  Overwriting half of a
   double-width character blanks the other half.  */
static void
set_cell (struct canvas *cv, int y, int x, wchar_t wc, int st, int cw)
{
  if (y < 0 || y >= cv->h || x < 0 || x >= cv->w)
    return;
  struct cell *row = cv->cells + (size_t) y * cv->w;
  if (cw == 2 && x + 1 >= cv->w)
    {
      wc = L' ';
      cw = 1;
    }
  if (row[x].ch == 0 && x > 0)
    row[x - 1].ch = L' ';
  int after = x + cw;
  if (after < cv->w && row[after].ch == 0)
    row[after].ch = L' ';
  row[x].ch = wc;
  row[x].st = st;
  if (cw == 2)
    {
      row[x + 1].ch = 0;
      row[x + 1].st = st;
    }
}

void
canvas_putwc (struct canvas *cv, int y, int x, wchar_t wc, int st)
{
  set_cell (cv, y, x, wc, st, wcwidth (wc) == 2 ? 2 : 1);
}

void
canvas_repeat (struct canvas *cv, int y, int x, int n, wchar_t wc, int st)
{
  for (int i = 0; i < n; i++)
    canvas_putwc (cv, y, x + i, wc, st);
}

void
canvas_fill (struct canvas *cv, struct rect r, int st)
{
  for (int y = r.y; y < r.y + r.h; y++)
    canvas_repeat (cv, y, r.x, r.w, L' ', st);
}

/* Draw S at Y, X in at most MAXW columns, or only measure it if CV is
   NULL.  Return the columns used.  */
static int
put_or_measure (struct canvas *cv, int y, int x, int maxw, const char *s,
                int st)
{
  mbstate_t ps;
  size_t len = strlen (s);
  int col = 0;

  memset (&ps, 0, sizeof ps);
  while (len > 0)
    {
      wchar_t wc;
      size_t n = mbrtowc (&wc, s, len, &ps);
      if (n == (size_t) -1 || n == (size_t) -2)
        {
          wc = L'?';
          n = 1;
          memset (&ps, 0, sizeof ps);
        }
      else if (n == 0)
        break;
      s += n;
      len -= n;

      int cw = wcwidth (wc);
      if (cw < 0)
        {
          wc = L'?';
          cw = 1;
        }
      if (cw == 0)
        continue;		/* Combining characters are dropped.  */
      if (col + cw > maxw)
        break;
      if (cv)
        set_cell (cv, y, x + col, wc, st, cw);
      col += cw;
    }
  return col;
}

int
text_width (const char *s)
{
  return put_or_measure (NULL, 0, 0, INT_MAX, s, 0);
}

int
canvas_put (struct canvas *cv, int y, int x, int maxw, const char *s, int st)
{
  if (maxw < 0)
    maxw = cv->w - x;
  if (maxw <= 0)
    return 0;
  return put_or_measure (cv, y, x, maxw, s, st);
}

int
canvas_put_center (struct canvas *cv, int y, int x, int w, const char *s,
                   int st)
{
  int tw = text_width (s);
  if (tw > w)
    tw = w;
  int off = (w - tw) / 2;
  return canvas_put (cv, y, x + off, w - off, s, st);
}

int
canvas_put_right (struct canvas *cv, int y, int x, int w, const char *s,
                  int st)
{
  int tw = text_width (s);
  if (tw > w)
    tw = w;
  return canvas_put (cv, y, x + w - tw, tw, s, st);
}

void
canvas_box (struct canvas *cv, struct rect r, int st, bool dbl)
{
  if (r.h < 2 || r.w < 2)
    return;
  wchar_t hz = dbl ? L'═' : L'─';
  wchar_t vt = dbl ? L'║' : L'│';
  int bottom = r.y + r.h - 1, right = r.x + r.w - 1;

  canvas_putwc (cv, r.y, r.x, dbl ? L'╔' : L'┌', st);
  canvas_repeat (cv, r.y, r.x + 1, r.w - 2, hz, st);
  canvas_putwc (cv, r.y, right, dbl ? L'╗' : L'┐', st);
  for (int y = r.y + 1; y < bottom; y++)
    {
      canvas_putwc (cv, y, r.x, vt, st);
      canvas_putwc (cv, y, right, vt, st);
    }
  canvas_putwc (cv, bottom, r.x, dbl ? L'╚' : L'└', st);
  canvas_repeat (cv, bottom, r.x + 1, r.w - 2, hz, st);
  canvas_putwc (cv, bottom, right, dbl ? L'╝' : L'┘', st);
}

void
canvas_title (struct canvas *cv, int y, int x, int w, const char *title,
              int st)
{
  int avail = w - 4;
  if (avail < 3)
    return;
  int tw = text_width (title) + 2;
  if (tw > avail)
    tw = avail;
  int tx = x + (w - tw) / 2;
  canvas_putwc (cv, y, tx, L' ', st);
  int used = canvas_put (cv, y, tx + 1, tw - 2, title, st);
  canvas_putwc (cv, y, tx + 1 + used, L' ', st);
}

void
canvas_divider (struct canvas *cv, int y, int x, int w, int st)
{
  if (w < 2)
    return;
  canvas_putwc (cv, y, x, L'╟', st);
  canvas_repeat (cv, y, x + 1, w - 2, L'─', st);
  canvas_putwc (cv, y, x + w - 1, L'╢', st);
}

/* Replacements for when the locale cannot show the characters used for
   frames and bars.  */
static wchar_t
ascii_fallback (wchar_t wc)
{
  switch (wc)
    {
    case L'═': case L'─':
      return L'-';
    case L'║': case L'│':
      return L'|';
    case L'╔': case L'╗': case L'╚': case L'╝':
    case L'┌': case L'┐': case L'└': case L'┘':
    case L'╟': case L'╢':
      return L'+';
    case L'░': case L'·':
      return L'.';
    case L'►':
      return L'>';
    case L'•':
      return L'*';
    default:
      if (wc >= 0x2581 && wc <= 0x2588)	/* Block elements.  */
        return L'#';
      return wc;
    }
}

void
canvas_flush (const struct canvas *cv, WINDOW *win)
{
  for (int y = 0; y < cv->h; y++)
    {
      const struct cell *row = cv->cells + (size_t) y * cv->w;
      for (int x = 0; x < cv->w; x++)
        {
          if (row[x].ch == 0)
            continue;
          wchar_t wc[2];
          wc[0] = cv->utf8 ? row[x].ch : ascii_fallback (row[x].ch);
          wc[1] = L'\0';
          attr_t attr;
          short pair;
          theme_style (row[x].st, &attr, &pair);
          cchar_t cc;
          setcchar (&cc, wc, attr, pair, NULL);
          /* Writing the bottom-right cell reports an error after
             drawing it; that is harmless.  */
          mvwadd_wch (win, y, x, &cc);
        }
    }
}

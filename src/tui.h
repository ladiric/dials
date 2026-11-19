/* tui.h -- include the wide-character curses header.
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

#ifndef DIALS_TUI_H
#define DIALS_TUI_H

#ifndef NCURSES_WIDECHAR
# define NCURSES_WIDECHAR 1
#endif

#if HAVE_NCURSESW_CURSES_H
# include <ncursesw/curses.h>
#else
# include <curses.h>
#endif

#endif /* DIALS_TUI_H */

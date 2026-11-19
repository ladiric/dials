# Configure template for Dials.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials.
#
# Dials is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Dials is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Dials.  If not, see <https://www.gnu.org/licenses/>.

AC_PREREQ([2.71])
AC_INIT([Dials], [0.1.0], [], [dials])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.16 gnu subdir-objects -Wall -Werror dist-xz])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS

# Native Language Support.
AM_GNU_GETTEXT_REQUIRE_VERSION([0.21])
AM_GNU_GETTEXT([external])

# The wide-character curses library.
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([NCURSES], [ncursesw], [],
  [AC_CHECK_LIB([ncursesw], [initscr], [NCURSES_LIBS=-lncursesw],
     [AC_MSG_ERROR([the wide-character curses library (ncursesw) is required])])])
dials_save_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$CPPFLAGS $NCURSES_CFLAGS"
AC_CHECK_HEADERS([ncursesw/curses.h curses.h], [break])
CPPFLAGS=$dials_save_CPPFLAGS

# Dials reads /proc and /sys and uses Linux timer file descriptors.
AC_CHECK_HEADERS([sys/timerfd.h], [],
  [AC_MSG_ERROR([sys/timerfd.h is missing; Dials currently supports GNU/Linux only])])

# NVIDIA's monitoring library is loaded at run time when present.
AC_SEARCH_LIBS([dlopen], [dl], [],
  [AC_MSG_ERROR([dlopen is required])])

# Used to generate the manual page from --help.
AM_MISSING_PROG([HELP2MAN], [help2man])

AC_ARG_ENABLE([gcc-warnings],
  [AS_HELP_STRING([--enable-gcc-warnings],
     [turn on many GCC warnings and treat them as errors (for developers)])],
  [], [enable_gcc_warnings=no])
WARN_CFLAGS=
AS_IF([test "x$enable_gcc_warnings" = xyes],
  [WARN_CFLAGS="-Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes \
-Wmissing-prototypes -Wpointer-arith -Wwrite-strings -Werror"])
AC_SUBST([WARN_CFLAGS])

AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile po/Makefile.in])
AC_OUTPUT

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
gl_EARLY

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

# NVIDIA's monitoring library, NVML, is not free software, so reading
# GPUs through it must be asked for.  It is loaded at run time when
# present, and is never needed to build.
AC_ARG_ENABLE([nvml],
  [AS_HELP_STRING([--enable-nvml],
     [read NVIDIA GPUs through NVIDIA's proprietary NVML library, when
      it is installed (default: no)])],
  [case $enableval in
     yes|no) ;;
     *) AC_MSG_ERROR([bad value $enableval for nvml option]) ;;
   esac],
  [enable_nvml=no])
AS_IF([test "$enable_nvml" = yes],
  [AC_SEARCH_LIBS([dlopen], [dl], [],
     [AC_MSG_ERROR([--enable-nvml needs dlopen])])
   AC_DEFINE([ENABLE_NVML], [1],
     [Define to 1 to read NVIDIA GPUs through NVML.])])
AM_CONDITIONAL([ENABLE_NVML], [test "$enable_nvml" = yes])

# Used to generate the manual page from --help.
AM_MISSING_PROG([HELP2MAN], [help2man])

AC_ARG_ENABLE([gcc-warnings],
  [AS_HELP_STRING([--enable-gcc-warnings],
     [turn on many GCC warnings and treat them as errors (for developers)])],
  [case $enableval in
     yes|no) ;;
     *) AC_MSG_ERROR([bad value $enableval for gcc-warnings option]) ;;
   esac
   dials_gcc_warnings=$enableval],
  [dials_gcc_warnings=no])

AS_IF([test "$dials_gcc_warnings" = yes],
  [gl_WARN_ADD([-Werror], [WERROR_CFLAGS])
   # Every warning GCC knows, less those that do not suit this code.
   ws=
   gl_MANYWARN_ALL_GCC([ws])
   nw=
   nw="$nw -Wsystem-headers"         # System headers are not ours.
   nw="$nw -Wvla"                    # Variable length arrays are fine.
   nw="$nw -Wpadded"                 # Struct padding is fine.
   nw="$nw -Wtraditional"            # Old compilers are not a target.
   nw="$nw -Wtraditional-conversion"
   nw="$nw -Wc++-compat"             # Dials is C, not C++.
   nw="$nw -Wdouble-promotion"       # Floats are widened on purpose.
   nw="$nw -Wfloat-equal"            # Exact float comparisons are on purpose.
   nw="$nw -Wswitch-enum"            # Defaults cover the other cases.
   nw="$nw -Wswitch-default"
   nw="$nw -Wunused-macros"          # Headers define macros for others.
   nw="$nw -Wsign-conversion"        # Too many harmless cases.
   nw="$nw -Wconversion"
   nw="$nw -Wformat-nonliteral"      # Formats are passed through gettext.
   nw="$nw -Wstack-protector"
   nw="$nw -Winline"
   nw="$nw -fanalyzer"               # Slow, and wrong too often.
   nw="$nw -Wformat-truncation=2"    # Text is cut to fit the screen.
   gl_MANYWARN_COMPLEMENT([ws], [$ws], [$nw])
   AS_VAR_APPEND([ws], [' -Wno-sign-compare'])
   AS_VAR_APPEND([ws], [' -Wno-unused-parameter'])
   for w in $ws; do
     gl_WARN_ADD([$w])
   done
   AC_SUBST([WARN_CFLAGS])])

gl_INIT

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile doc/Makefile po/Makefile.in])
AC_OUTPUT

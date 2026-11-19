# Makefile.am for the Dials program.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

bin_PROGRAMS = dials

dials_SOURCES = \
  app.c app.h \
  canvas.c canvas.h \
  gettext.h \
  gpu.c gpu.h gpu_demo.c gpu_nvml.c \
  main.c \
  metric.c metric.h \
  page_gpu.c page_overview.c page_placeholder.c pages.c pages.h \
  source.c source.h source_demo.c source_linux.c \
  system.h \
  theme.c theme.h \
  tui.h \
  ui.c ui.h \
  util.c util.h \
  widgets.c widgets.h

AM_CPPFLAGS = -DLOCALEDIR='"$(localedir)"'
AM_CFLAGS = $(NCURSES_CFLAGS) $(WARN_CFLAGS)
dials_LDADD = $(NCURSES_LIBS) $(LIBINTL)

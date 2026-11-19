# Makefile.am for the Dials program.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

bin_PROGRAMS = dials

dials_SOURCES = \
  app.c app.h \
  canvas.c canvas.h \
  cpu.c cpu.h \
  disk.c disk.h \
  gettext.h \
  gpu.c gpu.h gpu_demo.c gpu_nvml.c \
  main.c \
  memory.c memory.h \
  metric.c metric.h \
  net.c net.h \
  power.c power.h \
  procs.c procs.h \
  page_cpu.c page_disk.c page_gpu.c page_memory.c page_net.c \
  page_overview.c page_power.c page_proc.c page_sensors.c \
  pages.c pages.h \
  sensors.c sensors.h \
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

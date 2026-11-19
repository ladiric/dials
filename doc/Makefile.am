# Makefile.am for the Dials documentation.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

info_TEXINFOS = dials.texi
dials_TEXINFOS = fdl.texi

dist_man_MANS = dials.1

# The manual page is generated from 'dials --help'; the Texinfo manual
# is the main documentation.
dials.1: $(top_srcdir)/src/main.c $(top_srcdir)/configure.ac
	$(AM_V_GEN)$(HELP2MAN) --output=$@ \
	  --name='full-screen system resource monitor' \
	  --info-page=dials \
	  $(top_builddir)/src/dials$(EXEEXT)

# Top-level Makefile.am for Dials.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

SUBDIRS = po src doc
ACLOCAL_AMFLAGS = -I m4

TESTS = \
  tests/version.sh \
  tests/help.sh \
  tests/options.sh \
  tests/once.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)
AM_TESTS_ENVIRONMENT = \
  DIALS='$(abs_top_builddir)/src/dials$(EXEEXT)'; export DIALS; \
  LC_ALL=C; export LC_ALL;

EXTRA_DIST = bootstrap README-hacking $(TESTS)

# Top-level Makefile.am for Dials.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

SUBDIRS = lib po src doc
ACLOCAL_AMFLAGS = -I m4

# Check the HTML manual in 'make distcheck', which needs no TeX.
AM_DISTCHECK_DVI_TARGET = html

TESTS = \
  tests/version.sh \
  tests/help.sh \
  tests/options.sh \
  tests/once.sh \
  tests/settings.sh \
  tests/sysroot.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = $(SHELL)
AM_TESTS_ENVIRONMENT = \
  DIALS='$(abs_top_builddir)/src/dials$(EXEEXT)'; export DIALS; \
  XDG_CONFIG_HOME='$(abs_top_builddir)/tests/no-such-dir'; \
  export XDG_CONFIG_HOME; \
  LC_ALL=C; export LC_ALL;

EXTRA_DIST = \
  $(TESTS) \
  autogen.sh \
  autopull.sh \
  bootstrap \
  bootstrap-funclib.sh \
  bootstrap.conf \
  cfg.mk \
  m4/gnulib-cache.m4 \
  README-hacking \
  tests/root

# The change log is the history of the source repository.
dist-hook: gen-ChangeLog

gen_start_date = 2026-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	$(AM_V_GEN)if test -e $(srcdir)/.git; then			\
	  LC_ALL=en_US.UTF-8 TZ=UTC0					\
	  $(top_srcdir)/build-aux/gitlog-to-changelog			\
	    --srcdir=$(srcdir) --since=$(gen_start_date)		\
	    > $(distdir)/ChangeLog.tmp &&				\
	  rm -f $(distdir)/ChangeLog &&					\
	  mv $(distdir)/ChangeLog.tmp $(distdir)/ChangeLog;		\
	fi

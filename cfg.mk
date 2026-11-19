# Settings for the maintainer rules of maint.mk, from Gnulib.
# Copyright (C) 2026 Richard L.
#
# This file is part of Dials, distributed under the GNU General
# Public License, version 3 or later.  See COPYING.

manual_title = Dials
gnulib_dir = $(srcdir)/gnulib

# Dials is not yet released, so none of its versions is the previous one.
VERSION_REGEXP = $(subst .,\.,$(VERSION))

# NEWS is still being written: nothing in it is released yet.
local-checks-to-skip = sc_immutable_NEWS

# The test snapshot holds copies of system files exactly as the kernel
# writes them, padding and all.
VC_LIST_ALWAYS_EXCLUDE_REGEX = ^tests/root/

# Commands in the GitHub workflows are not --help text.
exclude_file_name_regexp--sc_two_space_separator_in_usage = ^\.github/

# In a tree that is not configured there is nothing to clean, so let
# the cleaning targets succeed there; packaging tools run them first.
ifeq ($(wildcard Makefile),)
  _clean_goals = clean distclean maintainer-clean
  ifneq ($(filter $(_clean_goals),$(MAKECMDGOALS)),)
    .DEFAULT_GOAL := $(firstword $(MAKECMDGOALS))
    $(_clean_goals):
	@:
  endif
endif

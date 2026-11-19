/* util.h -- small helpers: memory, file reading and formatting.
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

#ifndef DIALS_UTIL_H
#define DIALS_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include "c-strtod.h"
#include "xalloc.h"

/* Copy at most LEN bytes of SRC into DST of SIZE bytes, always
   terminating DST.  */
void str_copy (char *dst, size_t size, const char *src, size_t len);

/* Like fprintf, but always with '.' as the decimal point, for files
   that programs read.  */
int c_fprintf (FILE *f, const char *format, ...)
  __attribute__ ((__format__ (__printf__, 2, 3)));

/* Copy the next blank-separated field at *S into BUF of SIZE bytes,
   cutting it short if needed, and move *S past it.  Return false if
   there is no field left.  */
bool next_field (char **s, char *buf, size_t size);

/* Open PATH read-only.  Return the descriptor, or -1.  */
int open_ro (const char *path);

/* Measurements come from /proc and /sys under this directory: "" for
   the running system, or a copy of those files for testing.  */
void set_sysroot (const char *dir);

/* Whether a sysroot other than the running system is in use.  */
bool sysroot_set (void) ATTRIBUTE_PURE;

/* Store in BUF the system path PATH, such as "/proc/stat", under the
   sysroot.  */
void sys_path (char *buf, size_t size, const char *path);

/* Like open_ro and read_file, for a system path.  */
int sys_open (const char *path);
bool sys_read (const char *path, char *buf, size_t size);

/* Read the system file PATH holding one number into *OUT.  */
bool sys_number (const char *path, double *out);

/* Return the "avg10" figure of the KIND ("some" or "full") line of a
   pressure stall file read into BUF, or NaN.  */
double psi_avg10 (const char *buf, const char *kind);

/* Seconds on a clock that only moves forward.  */
double now_mono (void);

/* Read up to SIZE - 1 bytes from offset 0 of FD into BUF and terminate
   it.  Keeping /proc files open and rereading them this way is much
   cheaper than reopening them.  Return the byte count, or -1.  */
ssize_t read_at0 (int fd, char *buf, size_t size);

/* Read the start of the file PATH into BUF.  Return true on success.  */
bool read_file (const char *path, char *buf, size_t size);

void fmt_duration (char *buf, size_t size, double seconds);
void fmt_uptime (char *buf, size_t size, double seconds);
void fmt_interval (char *buf, size_t size, double seconds);
void fmt_mib (char *buf, size_t size, double mib);
void fmt_bytes (char *buf, size_t size, double bytes);
void fmt_rate (char *buf, size_t size, double bytes_per_second);
void fmt_count_rate (char *buf, size_t size, double per_second);
void fmt_mhz (char *buf, size_t size, double mhz);
void fmt_used_total (char *buf, size_t size, double used, double total);
void fmt_temp (char *buf, size_t size, double celsius);
void fmt_pct (char *buf, size_t size, double pct);
void fmt_watts (char *buf, size_t size, double watts);
void fmt_of_limit (char *buf, size_t size, double v, double limit,
                   const char *unit);

/* Whether the locale uses UTF-8, so that symbols such as the degree
   sign can be shown.  */
bool utf8_locale (void);

#endif /* DIALS_UTIL_H */

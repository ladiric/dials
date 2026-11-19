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
#include <sys/types.h>

void *xmalloc (size_t n);
void *xcalloc (size_t n, size_t s);
void close_stdout (void);

/* Copy at most LEN bytes of SRC into DST of SIZE bytes, always
   terminating DST.  */
void str_copy (char *dst, size_t size, const char *src, size_t len);

/* Like strtod, but always with '.' as the decimal point, as in the
   files of /proc, whatever the locale.  */
double c_strtod (const char *s, char **end);

/* Open PATH read-only.  Return the descriptor, or -1.  */
int open_ro (const char *path);

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
void fmt_used_total (char *buf, size_t size, double used, double total);

#endif /* DIALS_UTIL_H */

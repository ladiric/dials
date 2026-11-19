/* disk.c -- disk activity and file system space.
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

#include "system.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "disk.h"
#include "util.h"

/* Disks are found when the program starts; devices plugged in later are
   not shown.  Loop, RAM and zram devices are left out: they are not
   disks, and zram is shown with memory.  */
static const char *const skip_prefixes[] = { "loop", "ram", "zram" };

/* File systems on local storage.  Network file systems are left out,
   as measuring one that does not answer would stop the program.  */
static const char *const local_types[] = {
  "ext2", "ext3", "ext4", "xfs", "btrfs", "bcachefs", "f2fs", "jfs",
  "reiserfs", "vfat", "exfat", "ntfs", "ntfs3", "zfs", "fuseblk"
};

static double
rnd (struct disks *d)
{
  d->rng ^= d->rng << 13;
  d->rng ^= d->rng >> 7;
  d->rng ^= d->rng << 17;
  return (double) (d->rng >> 11) / 9007199254740992.0;
}

/* Remove white space at the end of S.  */
static void
trim (char *s)
{
  size_t n = strlen (s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\t'))
    s[--n] = '\0';
}

/* Open the temperature input of disk NAME, if its driver has one: NVMe
   drives have it under device/hwmonN, and SATA drives under
   device/hwmon/hwmonN with the drivetemp driver.  */
static int
open_disk_temp (const char *name)
{
  static const char *const dirs[] = { "device", "device/hwmon" };
  for (size_t i = 0; i < ARRAY_CARDINALITY (dirs); i++)
    {
      char rel[512], path[4096];
      snprintf (rel, sizeof rel, "/sys/block/%s/%s", name, dirs[i]);
      sys_path (path, sizeof path, rel);
      DIR *dir = opendir (path);
      if (!dir)
        continue;
      struct dirent *e;
      int fd = -1;
      while (fd < 0 && (e = readdir (dir)))
        if (strncmp (e->d_name, "hwmon", 5) == 0 && e->d_name[5] >= '0'
            && e->d_name[5] <= '9')
          {
            char in[1024];
            snprintf (in, sizeof in, "%s/%s/temp1_input", rel, e->d_name);
            fd = sys_open (in);
          }
      closedir (dir);
      if (fd >= 0)
        return fd;
    }
  return -1;
}

static int
by_name (const void *a, const void *b)
{
  const struct disk_dev *x = a, *y = b;
  return strcmp (x->name, y->name);
}

static void
add_disk (struct disks *d, const char *name)
{
  for (size_t i = 0; i < ARRAY_CARDINALITY (skip_prefixes); i++)
    if (strncmp (name, skip_prefixes[i], strlen (skip_prefixes[i])) == 0)
      return;

  char rel[512];
  double sectors;
  snprintf (rel, sizeof rel, "/sys/block/%s/size", name);
  if (!sys_number (rel, &sectors) || sectors <= 0 || d->n >= DISK_MAX)
    return;			/* Also skips empty card readers.  */

  struct disk_dev *dev = &d->dev[d->n++];
  str_copy (dev->name, sizeof dev->name, name, strlen (name));
  dev->size = sectors * 512;
  snprintf (rel, sizeof rel, "/sys/block/%s/device/model", name);
  if (sys_read (rel, dev->model, sizeof dev->model))
    trim (dev->model);
  double rot;
  snprintf (rel, sizeof rel, "/sys/block/%s/queue/rotational", name);
  dev->rotational = sys_number (rel, &rot) && rot > 0;
  dev->fd_temp = open_disk_temp (name);
}

static void
clear_readings (struct disk_dev *dev)
{
  dev->rd_bps = dev->wr_bps = dev->rd_iops = dev->wr_iops = NAN;
  dev->util = dev->await = dev->inflight = dev->temp = NAN;
}

void
disks_open (struct disks *d, bool demo, size_t history)
{
  memset (d, 0, sizeof *d);
  d->demo = demo;
  d->rng = UINT64_C (0x9E3779B97F4A7C15);
  d->fd_stats = -1;

  if (demo)
    {
      static const struct
      {
        const char *name, *model;
        double size;
        bool rot;
      } demo_disks[] = {
        { "nvme0n1", "Demo NVMe SSD 1TB", 1e12, false },
        { "sda", "Demo Hard Disk 4TB", 4e12, true },
      };
      for (size_t i = 0; i < ARRAY_CARDINALITY (demo_disks); i++)
        {
          struct disk_dev *dev = &d->dev[d->n++];
          snprintf (dev->name, sizeof dev->name, "%s", demo_disks[i].name);
          snprintf (dev->model, sizeof dev->model, "%s", demo_disks[i].model);
          dev->size = demo_disks[i].size;
          dev->rotational = demo_disks[i].rot;
          dev->fd_temp = -1;
        }
    }
  else
    {
      char path[4096];
      sys_path (path, sizeof path, "/sys/block");
      DIR *dir = opendir (path);
      if (dir)
        {
          struct dirent *e;
          while ((e = readdir (dir)))
            if (e->d_name[0] != '.')
              add_disk (d, e->d_name);
          closedir (dir);
        }
      qsort (d->dev, d->n, sizeof d->dev[0], by_name);
      d->fd_stats = sys_open ("/proc/diskstats");
    }

  for (int i = 0; i < d->n; i++)
    {
      clear_readings (&d->dev[i]);
      metric_init (&d->dev[i].m_rd, history);
      metric_init (&d->dev[i].m_wr, history);
      metric_init (&d->dev[i].m_util, history);
    }
  d->prev_time = now_mono ();
  disks_sample (d, false);	/* Rates need a first reading.  */
  disks_clear_history (d);
}

static struct disk_dev *
find_disk (struct disks *d, const char *name)
{
  for (int i = 0; i < d->n; i++)
    if (strcmp (d->dev[i].name, name) == 0)
      return &d->dev[i];
  return NULL;
}

static void
use_counters (struct disk_dev *dev, const unsigned long long *v, double dt)
{
  unsigned long long *p = dev->prev;
  if (dev->have_prev && dt > 0)
    {
      double d[DISK_COUNTERS];
      for (int k = 0; k < DISK_COUNTERS; k++)
        d[k] = v[k] >= p[k] ? (double) (v[k] - p[k]) : 0;
      /* 0 reads, 2 sectors read, 3 ms reading, 4 writes, 6 sectors
         written, 7 ms writing, 9 ms busy.  Sectors are 512 bytes.  */
      dev->rd_iops = d[0] / dt;
      dev->wr_iops = d[4] / dt;
      dev->rd_bps = d[2] * 512 / dt;
      dev->wr_bps = d[6] * 512 / dt;
      double busy = 100 * d[9] / (dt * 1000);
      dev->util = busy > 100 ? 100 : busy;
      double ops = d[0] + d[4];
      dev->await = ops > 0 ? (d[3] + d[7]) / ops : 0;
    }
  dev->inflight = (double) v[8];
  memcpy (p, v, sizeof dev->prev);
  dev->have_prev = true;
}

static void
read_stats (struct disks *d, double dt)
{
  if (read_at0 (d->fd_stats, d->buf, sizeof d->buf) <= 0)
    return;
  for (char *line = d->buf; line && *line;)
    {
      char *next = strchr (line, '\n');
      if (next)
        *next++ = '\0';
      unsigned int major, minor;
      char name[64];
      int used = 0;
      if (sscanf (line, " %u %u %63s%n", &major, &minor, name, &used) == 3)
        {
          struct disk_dev *dev = find_disk (d, name);
          if (dev)
            {
              unsigned long long v[DISK_COUNTERS] = { 0 };
              char *s = line + used, *end;
              for (int k = 0; k < DISK_COUNTERS; k++, s = end)
                {
                  v[k] = strtoull (s, &end, 10);
                  if (end == s)
                    break;
                }
              use_counters (dev, v, dt);
            }
        }
      line = next;
    }
}

/* Undo the octal escapes, such as \040 for a space, that the kernel
   uses in mount points.  */
static void
unescape (char *s)
{
  char *out = s;
  while (*s)
    {
      if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0'
          && s[2] <= '7' && s[3] >= '0' && s[3] <= '7')
        {
          *out++ = (char) ((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
          s += 4;
        }
      else
        *out++ = *s++;
    }
  *out = '\0';
}

static bool
local_type (const char *type)
{
  for (size_t i = 0; i < ARRAY_CARDINALITY (local_types); i++)
    if (strcmp (type, local_types[i]) == 0)
      return true;
  return false;
}

static void
read_fs (struct disks *d)
{
  d->nfs = 0;
  if (!sys_read ("/proc/self/mounts", d->buf, sizeof d->buf))
    return;
  for (char *line = d->buf; line && *line && d->nfs < FS_MAX;)
    {
      char *next = strchr (line, '\n');
      if (next)
        *next++ = '\0';
      char dev[128], mount[256], type[32];
      if (sscanf (line, "%127s %255s %31s", dev, mount, type) == 3
          && local_type (type))
        {
          /* A device mounted in several places, as btrfs subvolumes
             often are, is shown once.  */
          bool seen = false;
          for (int i = 0; i < d->nfs; i++)
            if (strcmp (d->fs[i].device, dev) == 0)
              seen = true;
          if (!seen)
            {
              struct fs_entry *f = &d->fs[d->nfs++];
              unescape (mount);
              snprintf (f->device, sizeof f->device, "%s", dev);
              snprintf (f->mount, sizeof f->mount, "%s", mount);
              snprintf (f->type, sizeof f->type, "%s", type);
              f->size = f->used = f->avail = NAN;
              /* A copy of /proc says nothing about free space.  */
              struct statvfs st;
              if (!sysroot_set () && statvfs (mount, &st) == 0
                  && st.f_blocks > 0)
                {
                  double unit = (double) st.f_frsize;
                  f->size = st.f_blocks * unit;
                  f->used = (st.f_blocks - st.f_bfree) * unit;
                  f->avail = st.f_bavail * unit;
                }
            }
        }
      line = next;
    }
}

static void
demo_sample (struct disks *d, bool fs)
{
  for (int i = 0; i < d->n; i++)
    {
      struct disk_dev *dev = &d->dev[i];
      double busy = rnd (d) < 0.2 ? rnd (d) : rnd (d) * 0.1;
      double speed = dev->rotational ? 180e6 : 2500e6;
      dev->rd_bps = busy * speed * rnd (d);
      dev->wr_bps = busy * speed * rnd (d) * 0.6;
      dev->rd_iops = dev->rd_bps / (dev->rotational ? 256e3 : 64e3);
      dev->wr_iops = dev->wr_bps / (dev->rotational ? 256e3 : 64e3);
      dev->util = busy * 100;
      dev->await = dev->rotational ? 4 + busy * 20 : 0.1 + busy;
      dev->inflight = busy > 0.5 ? 2 : 0;
      dev->temp = dev->rotational ? 34 + busy * 5 : 41 + busy * 15;
    }
  if (fs)
    {
      static const struct
      {
        const char *dev, *mount, *type;
        double size, used;
      } demo_fs[] = {
        { "/dev/nvme0n1p2", "/", "ext4", 500e9, 210e9 },
        { "/dev/nvme0n1p1", "/boot/efi", "vfat", 600e6, 20e6 },
        { "/dev/sda1", "/srv/archive", "xfs", 4e12, 3.7e12 },
      };
      d->nfs = ARRAY_CARDINALITY (demo_fs);
      for (int i = 0; i < d->nfs; i++)
        {
          struct fs_entry *f = &d->fs[i];
          snprintf (f->device, sizeof f->device, "%s", demo_fs[i].dev);
          snprintf (f->mount, sizeof f->mount, "%s", demo_fs[i].mount);
          snprintf (f->type, sizeof f->type, "%s", demo_fs[i].type);
          f->size = demo_fs[i].size;
          f->used = demo_fs[i].used;
          f->avail = f->size - f->used;
        }
    }
}

void
disks_sample (struct disks *d, bool fs)
{
  double now = now_mono (), dt = now - d->prev_time;
  d->prev_time = now;

  for (int i = 0; i < d->n; i++)
    clear_readings (&d->dev[i]);
  if (d->demo)
    demo_sample (d, fs);
  else
    {
      read_stats (d, dt);
      char buf[64];
      for (int i = 0; i < d->n; i++)
        if (read_at0 (d->dev[i].fd_temp, buf, sizeof buf) > 0)
          d->dev[i].temp = c_strtod (buf, NULL) / 1000;
      if (fs)
        read_fs (d);
    }

  for (int i = 0; i < d->n; i++)
    {
      struct disk_dev *dev = &d->dev[i];
      metric_push (&dev->m_rd, dev->rd_bps / 1048576);
      metric_push (&dev->m_wr, dev->wr_bps / 1048576);
      metric_push (&dev->m_util, dev->util);
    }
}

void
disks_clear_history (struct disks *d)
{
  for (int i = 0; i < d->n; i++)
    {
      metric_clear (&d->dev[i].m_rd);
      metric_clear (&d->dev[i].m_wr);
      metric_clear (&d->dev[i].m_util);
    }
}

void
disks_close (struct disks *d)
{
  for (int i = 0; i < d->n; i++)
    {
      if (d->dev[i].fd_temp >= 0)
        close (d->dev[i].fd_temp);
      metric_free (&d->dev[i].m_rd);
      metric_free (&d->dev[i].m_wr);
      metric_free (&d->dev[i].m_util);
    }
  if (d->fd_stats >= 0)
    close (d->fd_stats);
}

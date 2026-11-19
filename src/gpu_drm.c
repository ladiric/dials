/* gpu_drm.c -- AMD, Intel and other GPUs through the kernel's drivers.
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

#include <config.h>

#include "system.h"

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

#include "gpu.h"
#include "util.h"

/* Each driver puts its readings in different files under
   /sys/class/drm/cardN/device:

   amdgpu   gpu_busy_percent, mem_busy_percent, mem_info_vram_used and
            _total, pp_dpm_sclk and pp_dpm_mclk, and hwmon for
            temperature, power, fan and clocks;
   i915     no busy figure, but the time the GPU spent idle (RC6) in
            ../gt/gt0/rc6_residency_ms, and frequencies in ../gt_*;
   xe       the same idle time in tile0/gt0/gtidle/idle_residency_ms;
   nouveau  hwmon only.

   Cards that NVML already covers, NVIDIA's with its own driver, are
   left out.  */

enum { DRM_MAX = 8 };

enum drm_kind { DRM_AMD, DRM_I915, DRM_XE, DRM_NOUVEAU };

static const struct
{
  const char *driver;
  enum drm_kind kind;
} known[] = {
  { "amdgpu", DRM_AMD }, { "i915", DRM_I915 }, { "xe", DRM_XE },
  { "nouveau", DRM_NOUVEAU },
};

struct drm_card
{
  char card[16];		/* "card1".  */
  char hwmon[300];		/* The card's hwmon directory, or "".  */
  enum drm_kind kind;
  char driver[16];
  char name[96];
  double sclk_max, mclk_max;	/* MHz.  */
  double power_limit;		/* Watts.  */
  /* For figures worked out from counters: idle milliseconds, energy in
     microjoules, and when they were read.  */
  double prev_idle, prev_energy, prev_time;
};

struct drm
{
  int n;
  struct drm_card c[DRM_MAX];
};

/* Read the number in file NAME under the card's device directory,
   divided by SCALE, or NaN.  */
static double
dev_number (const struct drm_card *c, const char *name, double scale)
{
  char rel[512];
  double v;
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/%s", c->card, name);
  return sys_number (rel, &v) ? v / scale : NAN;
}

static double
card_number (const struct drm_card *c, const char *name, double scale)
{
  char rel[512];
  double v;
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/%s", c->card, name);
  return sys_number (rel, &v) ? v / scale : NAN;
}

static double
hwmon_number (const struct drm_card *c, const char *name, double scale)
{
  char rel[512];
  double v;
  if (!c->hwmon[0])
    return NAN;
  snprintf (rel, sizeof rel, "%s/%s", c->hwmon, name);
  return sys_number (rel, &v) ? v / scale : NAN;
}

/* Return the highest clock, in MHz, in an amdgpu pp_dpm_* file, whose
   lines read "0: 500Mhz", "1: 1800Mhz *".  */
static double
dpm_max (const struct drm_card *c, const char *name)
{
  char rel[512], buf[1024];
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/%s", c->card, name);
  if (!sys_read (rel, buf, sizeof buf))
    return NAN;
  double best = NAN;
  for (char *line = buf; line && *line;)
    {
      char *colon = strchr (line, ':');
      if (colon)
        {
          double v = c_strtod (colon + 1, NULL);
          if (isnan (best) || v > best)
            best = v;
        }
      line = strchr (line, '\n');
      if (line)
        line++;
    }
  return best;
}

/* Look up the name of PCI device VENDOR:DEVICE in the pci.ids database,
   if it is installed.  Use the part in brackets when there is one, as
   in "TU116 [GeForce GTX 1660]".  */
static bool
pci_name (unsigned int vendor, unsigned int device, char *buf, size_t size)
{
  static const char *const files[] = {
    "/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids"
  };
  for (size_t i = 0; i < ARRAY_CARDINALITY (files); i++)
    {
      char path[4096], line[512], want_vendor[8], want_device[8];
      sys_path (path, sizeof path, files[i]);
      FILE *f = fopen (path, "re");
      if (!f)
        continue;
      snprintf (want_vendor, sizeof want_vendor, "%04x ", vendor);
      snprintf (want_device, sizeof want_device, "\t%04x ", device);
      bool in_vendor = false, found = false;
      while (!found && fgets (line, sizeof line, f))
        {
          if (line[0] != '\t' && line[0] != '#')
            in_vendor = strncmp (line, want_vendor, 5) == 0;
          else if (in_vendor && strncmp (line, want_device, 6) == 0)
            {
              char *name = line + 6 + strspn (line + 6, " ");
              name[strcspn (name, "\n")] = '\0';
              char *open = strchr (name, '['), *close = strrchr (name, ']');
              if (open && close && close > open)
                {
                  *close = '\0';
                  name = open + 1;
                }
              str_copy (buf, size, name, strlen (name));
              found = true;
            }
        }
      fclose (f);
      if (found)
        return true;
    }
  return false;
}

/* Find the hwmon directory of the card, under device/hwmon/hwmonN.  */
static void
find_hwmon (struct drm_card *c)
{
  char rel[300], path[4096];
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/hwmon", c->card);
  sys_path (path, sizeof path, rel);
  DIR *dir = opendir (path);
  if (!dir)
    return;
  struct dirent *e;
  while ((e = readdir (dir)))
    if (strncmp (e->d_name, "hwmon", 5) == 0)
      {
        snprintf (c->hwmon, sizeof c->hwmon, "%.80s/%.32s", rel, e->d_name);
        break;
      }
  closedir (dir);
}

static void
add_card (struct drm *d, const char *card)
{
  char rel[300], buf[512];
  double vendor_id, device_id;

  /* "card1" but not "card1-HDMI-A-1", which is a connector.  */
  if (strncmp (card, "card", 4) != 0 || strchr (card, '-')
      || d->n >= DRM_MAX)
    return;
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/uevent", card);
  if (!sys_read (rel, buf, sizeof buf))
    return;
  char *drv = strstr (buf, "DRIVER=");
  if (!drv)
    return;
  drv += 7;
  drv[strcspn (drv, "\n")] = '\0';

  int k = -1;
  for (size_t i = 0; i < ARRAY_CARDINALITY (known); i++)
    if (streq (drv, known[i].driver))
      k = (int) i;
  if (k < 0)
    return;

  struct drm_card *c = &d->c[d->n++];
  memset (c, 0, sizeof *c);
  str_copy (c->card, sizeof c->card, card, strlen (card));
  str_copy (c->driver, sizeof c->driver, drv, strlen (drv));
  c->kind = known[k].kind;
  c->prev_idle = c->prev_energy = NAN;
  find_hwmon (c);

  /* The name: the driver's own, the PCI database's, or the maker's.  */
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/vendor", card);
  if (!sys_number (rel, &vendor_id))
    vendor_id = 0;
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/device", card);
  if (!sys_number (rel, &device_id))
    device_id = 0;
  const char *maker = vendor_id == 0x1002 ? "AMD"
    : vendor_id == 0x8086 ? "Intel" : vendor_id == 0x10de ? "NVIDIA" : "";
  char product[96] = "";
  snprintf (rel, sizeof rel, "/sys/class/drm/%s/device/product_name", card);
  if (sys_read (rel, buf, sizeof buf))
    {
      buf[strcspn (buf, "\n")] = '\0';
      str_copy (product, sizeof product, buf, strlen (buf));
    }
  if (!product[0])
    pci_name ((unsigned int) vendor_id, (unsigned int) device_id, product,
              sizeof product);
  if (product[0] && strncmp (product, maker, strlen (maker)) != 0)
    snprintf (c->name, sizeof c->name, "%s %s", maker, product);
  else if (product[0])
    snprintf (c->name, sizeof c->name, "%s", product);
  else
    snprintf (c->name, sizeof c->name, _("%s GPU"), maker[0] ? maker : drv);

  /* Values that do not change.  */
  c->sclk_max = c->mclk_max = c->power_limit = NAN;
  if (c->kind == DRM_AMD)
    {
      c->sclk_max = dpm_max (c, "pp_dpm_sclk");
      c->mclk_max = dpm_max (c, "pp_dpm_mclk");
    }
  else if (c->kind == DRM_I915)
    c->sclk_max = card_number (c, "gt_max_freq_mhz", 1);
  else if (c->kind == DRM_XE)
    c->sclk_max = dev_number (c, "tile0/gt0/freq0/max_freq", 1);
  c->power_limit = hwmon_number (c, "power1_cap", 1e6);
  if (isnan (c->power_limit))
    c->power_limit = hwmon_number (c, "power1_max", 1e6);
}

static int
by_card (const void *a, const void *b)
{
  const struct drm_card *x = a, *y = b;
  long i = strtol (x->card + 4, NULL, 10), j = strtol (y->card + 4, NULL, 10);
  return (i > j) - (i < j);
}

static bool
drm_open (void **priv, char *err, size_t esize)
{
  char path[4096];
  (void) err;
  (void) esize;
  sys_path (path, sizeof path, "/sys/class/drm");
  DIR *dir = opendir (path);
  if (!dir)
    return false;
  struct drm *d = xcalloc (1, sizeof *d);
  struct dirent *e;
  while ((e = readdir (dir)))
    add_card (d, e->d_name);
  closedir (dir);
  qsort (d->c, d->n, sizeof d->c[0], by_card);
  *priv = d;
  return true;
}

static int
drm_count (void *priv)
{
  return ((struct drm *) priv)->n;
}

static void
drm_describe (void *priv, int i, char *name, size_t nsize, char *driver,
              size_t dsize)
{
  struct drm *d = priv;
  snprintf (name, nsize, "%s", d->c[i].name);
  snprintf (driver, dsize, "%s", d->c[i].driver);
}

/* The share of the time since the last reading that the GPU was not
   idle, from a counter of idle milliseconds.  */
static double
busy_from_idle (struct drm_card *c, double idle_ms, double now)
{
  double busy = NAN;
  if (!isnan (idle_ms) && !isnan (c->prev_idle) && now > c->prev_time)
    {
      double idle = (idle_ms - c->prev_idle) / ((now - c->prev_time) * 1000);
      busy = 100 * (1 - (idle < 0 ? 0 : idle > 1 ? 1 : idle));
    }
  c->prev_idle = idle_ms;
  return busy;
}

static void
drm_sample (void *priv, int i, bool details, struct gpu_sample *s)
{
  struct drm *d = priv;
  struct drm_card *c = &d->c[i];
  double now = now_mono ();

  s->power_limit = c->power_limit;
  s->clock_sm_max = c->sclk_max;
  s->clock_mem_max = c->mclk_max;

  s->temp = hwmon_number (c, "temp1_input", 1000);
  s->power = hwmon_number (c, "power1_average", 1e6);
  if (isnan (s->power))
    s->power = hwmon_number (c, "power1_input", 1e6);
  if (isnan (s->power))
    {
      /* Some drivers count energy instead.  */
      double uj = hwmon_number (c, "energy1_input", 1);
      if (!isnan (uj) && !isnan (c->prev_energy) && now > c->prev_time
          && uj >= c->prev_energy)
        s->power = (uj - c->prev_energy) / 1e6 / (now - c->prev_time);
      c->prev_energy = uj;
    }

  switch (c->kind)
    {
    case DRM_AMD:
      s->util = dev_number (c, "gpu_busy_percent", 1);
      s->mem_util = dev_number (c, "mem_busy_percent", 1);
      s->mem_used = dev_number (c, "mem_info_vram_used", 1048576);
      s->mem_total = dev_number (c, "mem_info_vram_total", 1048576);
      break;
    case DRM_I915:
      s->util = busy_from_idle (c, card_number (c, "gt/gt0/rc6_residency_ms",
                                                1), now);
      break;
    case DRM_XE:
      s->util = busy_from_idle (c,
                                dev_number (c, "tile0/gt0/gtidle/"
                                            "idle_residency_ms", 1), now);
      break;
    case DRM_NOUVEAU:
      break;
    }
  c->prev_time = now;
  if (!details)
    return;

  switch (c->kind)
    {
    case DRM_AMD:
      s->clock_sm = hwmon_number (c, "freq1_input", 1e6);
      s->clock_mem = hwmon_number (c, "freq2_input", 1e6);
      break;
    case DRM_I915:
      s->clock_sm = card_number (c, "gt_act_freq_mhz", 1);
      break;
    case DRM_XE:
      s->clock_sm = dev_number (c, "tile0/gt0/freq0/act_freq", 1);
      break;
    case DRM_NOUVEAU:
      break;
    }
  double pwm = hwmon_number (c, "pwm1", 1);
  if (!isnan (pwm))
    s->fan = 100 * pwm / 255;
}

static void
drm_close (void *priv)
{
  free (priv);
}

const struct gpu_backend gpu_drm_backend = {
  drm_open, drm_count, drm_describe, drm_sample, drm_close
};

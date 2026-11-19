/* main.c -- command line handling for Dials.
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

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "canvas.h"
#include "cpu.h"
#include "disk.h"
#include "gpu.h"
#include "memory.h"
#include "pages.h"
#include "source.h"
#include "theme.h"
#include "util.h"

enum
{
  ONCE_OPTION = CHAR_MAX + 1,
  SYSROOT_OPTION,
  HELP_OPTION,
  VERSION_OPTION
};

static const struct option long_options[] = {
  { "demo", no_argument, NULL, 'd' },
  { "interval", required_argument, NULL, 'i' },
  { "page", required_argument, NULL, 'p' },
  { "theme", required_argument, NULL, 't' },
  { "once", no_argument, NULL, ONCE_OPTION },
  { "sysroot", required_argument, NULL, SYSROOT_OPTION },
  { "help", no_argument, NULL, HELP_OPTION },
  { "version", no_argument, NULL, VERSION_OPTION },
  { NULL, 0, NULL, 0 }
};

static void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    fprintf (stderr, _("Try '%s --help' for more information.\n"),
             program_invocation_short_name);
  else
    {
      printf (_("Usage: %s [OPTION]...\n"), program_invocation_short_name);
      fputs (_("\
Monitor the resources of this system in a full-screen terminal interface.\n\
\n"), stdout);
      fputs (_("\
  -d, --demo               show simulated data instead of this system's\n\
  -i, --interval=SECONDS   take a sample every SECONDS (default 1)\n\
  -p, --page=NAME          start on page NAME\n\
  -t, --theme=NAME         use the color theme NAME (default classic)\n\
      --once               print one plain-text report and exit\n\
      --sysroot=DIR        read /proc and /sys under DIR (for testing)\n\
      --help               display this help and exit\n\
      --version            output version information and exit\n"),
             stdout);

      fputs (_("\nPages:"), stdout);
      for (size_t i = 0; i < page_count; i++)
        printf (" %s", pages[i].id);
      fputs (_("\nThemes:"), stdout);
      for (int i = 0; i < theme_count (); i++)
        printf (" %s", theme_name (i));
      fputs (_("\n\
\n\
While the program runs, press F1 for the list of keys and F10 to quit.\n"),
             stdout);

      if (PACKAGE_BUGREPORT[0])
        printf (_("\nReport bugs to: %s\n"), PACKAGE_BUGREPORT);
      if (PACKAGE_URL[0])
        printf (_("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL);
    }
  exit (status);
}

static void
version (void)
{
  printf ("%s (%s) %s\n", PACKAGE, PACKAGE_NAME, VERSION);
  puts ("Copyright (C) 2026 Richard L.");
  printf (_("License GPLv3+: GNU GPL version 3 or later <%s>.\n"),
          "https://gnu.org/licenses/gpl.html");
  fputs (_("\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n"), stdout);
}

static bool
parse_interval (const char *arg, double *out)
{
  char *end;
  errno = 0;
  /* Accept the locale's decimal point, and '.' everywhere.  */
  double v = strtod (arg, &end);
  if (*end)
    {
      errno = 0;
      v = c_strtod (arg, &end);
    }
  if (errno || end == arg || *end || !(v >= 0.1 && v <= 3600))
    return false;
  *out = v;
  return true;
}

/* Lines of the plain-text report.  */
struct report
{
  int n;
  char label[64][128];
  char value[64][256];
};

static void
add_line (struct report *r, const char *label, const char *value)
{
  if (r->n >= 64)
    return;
  str_copy (r->label[r->n], sizeof r->label[0], label, strlen (label));
  str_copy (r->value[r->n], sizeof r->value[0], value, strlen (value));
  r->n++;
}

/* Print a plain-text report, for scripts and for terminals that cannot
   run the full-screen interface.  */
static void
report (struct source *src, bool demo)
{
  static struct cpu cpu;
  static struct memory mem;
  static struct disks disks;
  static struct gpus gpus;
  static struct report r;
  struct sys_info in;
  struct sys_sample s;
  struct timespec pause = { 0, 250000000 };
  char v[256], a1[64], a2[64], a3[64];

  source_info (src, &in);
  cpu_open (&cpu, demo, 1);
  memory_open (&mem, demo, 1);
  disks_open (&disks, demo, 1);
  gpus_open (&gpus, demo, 1);
  nanosleep (&pause, NULL);	/* Use and rates need two readings.  */
  source_sample (src, &s);
  cpu_sample (&cpu);
  memory_sample (&mem, false);
  disks_sample (&disks, true);
  gpus_sample (&gpus, false);

  add_line (&r, _("Host"), in.host);
  add_line (&r, _("System"), in.os);
  snprintf (v, sizeof v, "%s %s", in.kernel, in.arch);
  add_line (&r, _("Kernel"), v);
  add_line (&r, _("Processor"), in.cpu_model);
  snprintf (v, sizeof v, "%d", cpu.n);
  add_line (&r, _("CPUs"), v);
  fmt_uptime (v, sizeof v, s.uptime);
  add_line (&r, _("Uptime"), v);
  if (isnan (s.load[0]))
    snprintf (v, sizeof v, "%s", _("n/a"));
  else
    snprintf (v, sizeof v, "%.2f %.2f %.2f", s.load[0], s.load[1], s.load[2]);
  add_line (&r, _("Load"), v);

  fmt_pct (a1, sizeof a1, cpu.total);
  if (isnan (cpu.total))
    snprintf (v, sizeof v, "%s", a1);
  else
    snprintf (v, sizeof v, _("%s (user %.0f%%, system %.0f%%, I/O wait %.0f%%)"),
              a1, cpu.split.user, cpu.split.system, cpu.split.iowait);
  add_line (&r, _("CPU"), v);
  if (!isnan (cpu.mhz_avg))
    {
      fmt_mhz (a1, sizeof a1, cpu.mhz_avg);
      fmt_mhz (a2, sizeof a2, cpu.mhz_max);
      snprintf (v, sizeof v, _("%s average, %s maximum"), a1, a2);
      add_line (&r, _("Frequency"), v);
    }
  if (!isnan (cpu.temp))
    {
      fmt_temp (v, sizeof v, cpu.temp);
      add_line (&r, _("Temperature"), v);
    }

  fmt_used_total (a1, sizeof a1, mem.used, mem.total);
  fmt_mib (a2, sizeof a2, mem.available);
  if (mem.total > 0)
    snprintf (v, sizeof v, _("%s (%.0f%%), %s available"), a1,
              100 * mem.used / mem.total, a2);
  else
    snprintf (v, sizeof v, "%s", a1);
  add_line (&r, _("Memory"), v);
  if (mem.swap_total > 0)
    {
      fmt_used_total (a1, sizeof a1, mem.swap_used, mem.swap_total);
      snprintf (v, sizeof v, "%s (%.0f%%)", a1,
                100 * mem.swap_used / mem.swap_total);
    }
  else
    snprintf (v, sizeof v, "%s", _("no swap"));
  add_line (&r, _("Swap"), v);

  /* One line per GPU, or why there is none.  */
  for (int i = 0; i < gpus.count; i++)
    {
      const struct gpu_sample *gs = &gpus.dev[i].cur;
      char label[48], temp[32], util[16], vram[64], power[48];
      fmt_temp (temp, sizeof temp, gs->temp);
      fmt_pct (util, sizeof util, gs->util);
      fmt_used_total (vram, sizeof vram, gs->mem_used, gs->mem_total);
      fmt_of_limit (power, sizeof power, gs->power, gs->power_limit, "W");
      snprintf (label, sizeof label, _("GPU %d"), i);
      snprintf (v, sizeof v, "%s, %s, %s, %s, %s",
                gpus.dev[i].name, temp, util, vram, power);
      add_line (&r, label, v);
    }
  if (gpus.count == 0)
    add_line (&r, _("GPU"), gpus.error);

  for (int i = 0; i < disks.n; i++)
    {
      const struct disk_dev *d = &disks.dev[i];
      char label[64];
      snprintf (label, sizeof label, _("Disk %s"), d->name);
      fmt_bytes (a1, sizeof a1, d->size);
      const char *kind = d->rotational ? _("hard disk") : _("solid state");
      if (isnan (d->temp))
        snprintf (v, sizeof v, "%s, %s, %s",
                  d->model[0] ? d->model : "?", a1, kind);
      else
        {
          fmt_temp (a2, sizeof a2, d->temp);
          snprintf (v, sizeof v, "%s, %s, %s, %s",
                    d->model[0] ? d->model : "?", a1, kind, a2);
        }
      add_line (&r, label, v);
    }
  for (int i = 0; i < disks.nfs; i++)
    {
      const struct fs_entry *f = &disks.fs[i];
      fmt_bytes (a1, sizeof a1, f->used);
      fmt_bytes (a2, sizeof a2, f->size);
      fmt_bytes (a3, sizeof a3, f->avail);
      if (f->size > 0)
        snprintf (v, sizeof v, _("%s, %s of %s used (%.0f%%), %s free"),
                  f->type, a1, a2, 100 * f->used / f->size, a3);
      else
        snprintf (v, sizeof v, "%s, %s", f->type, f->device);
      add_line (&r, f->mount, v);
    }

  int width = 0;
  for (int i = 0; i < r.n; i++)
    {
      int w = text_width (r.label[i]);
      if (w > width)
        width = w;
    }
  for (int i = 0; i < r.n; i++)
    {
      int pad = width - text_width (r.label[i]);
      printf ("%s:%*s %s\n", r.label[i], pad > 0 ? pad : 0, "", r.value[i]);
    }

  cpu_close (&cpu);
  memory_close (&mem);
  disks_close (&disks);
  gpus_close (&gpus);
}

int
main (int argc, char **argv)
{
  bool demo = false, once = false;
  double interval = 1.0;
  int page = 0, theme = 0;
  int c;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
  atexit (close_stdout);

  while ((c = getopt_long (argc, argv, "di:p:t:", long_options, NULL)) != -1)
    switch (c)
      {
      case 'd':
        demo = true;
        break;
      case 'i':
        if (!parse_interval (optarg, &interval))
          error (EXIT_FAILURE, 0,
                 _("invalid interval '%s'; use 0.1 to 3600 seconds"), optarg);
        break;
      case 'p':
        page = page_find (optarg);
        if (page < 0)
          error (EXIT_FAILURE, 0,
                 _("unknown page '%s'; see '%s --help' for the list"),
                 optarg, program_invocation_short_name);
        break;
      case 't':
        theme = theme_find (optarg);
        if (theme < 0)
          error (EXIT_FAILURE, 0,
                 _("unknown theme '%s'; see '%s --help' for the list"),
                 optarg, program_invocation_short_name);
        break;
      case ONCE_OPTION:
        once = true;
        break;
      case SYSROOT_OPTION:
        set_sysroot (optarg);
        break;
      case HELP_OPTION:
        usage (EXIT_SUCCESS);
        break;
      case VERSION_OPTION:
        version ();
        return EXIT_SUCCESS;
      default:
        usage (EXIT_FAILURE);
        break;
      }
  if (optind < argc)
    {
      error (0, 0, _("extra operand '%s'"), argv[optind]);
      usage (EXIT_FAILURE);
    }

  struct source *src = source_new (demo);
  if (!src)
    error (EXIT_FAILURE, errno, "%s", _("cannot read system statistics"));

  if (once)
    {
      report (src, demo);
      source_free (src);
      return EXIT_SUCCESS;
    }

  if (!isatty (STDIN_FILENO) || !isatty (STDOUT_FILENO))
    error (EXIT_FAILURE, 0, "%s",
           _("the display needs a terminal; use --once for a text report"));

  struct app a;
  app_init (&a, src, interval, (size_t) page, theme, demo);
  int sig = app_run (&a);
  app_fini (&a);
  source_free (src);

  /* Die from the signal that stopped us, so the caller sees it.  */
  if (sig)
    {
      signal (sig, SIG_DFL);
      raise (sig);
    }
  return EXIT_SUCCESS;
}

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

#include <config.h>

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
#include "net.h"
#include "power.h"
#include "procs.h"
#include "sensors.h"
#include "settings.h"
#include "pages.h"
#include "source.h"
#include "theme.h"
#include "closeout.h"
#include "util.h"

enum
{
  ONCE_OPTION = CHAR_MAX + 1,
  LOG_OPTION,
  SYSROOT_OPTION,
  HELP_OPTION,
  VERSION_OPTION
};

static const struct option long_options[] = {
  { "demo", no_argument, NULL, 'd' },
  { "interval", required_argument, NULL, 'i' },
  { "page", required_argument, NULL, 'p' },
  { "theme", required_argument, NULL, 't' },
  { "log", required_argument, NULL, LOG_OPTION },
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
      --log=FILE           log each sample to FILE: CSV, or JSON lines\n\
                             if FILE ends in .json or .jsonl\n\
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
While the program runs, press F1 for the list of keys and F10 to quit.\n\
Settings saved with Options > Save setup are read from\n\
$XDG_CONFIG_HOME/dials/dials.conf, or ~/.config/dials/dials.conf.\n"),
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
  static struct net net;
  static struct procs procs;
  static struct sensors sensors;
  static struct power power;
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
  net_open (&net, demo, 1);
  procs_open (&procs, demo);
  sensors_open (&sensors, demo, 1);
  power_open (&power, demo, 1);
  nanosleep (&pause, NULL);	/* Use and rates need two readings.  */
  source_sample (src, &s);
  cpu_sample (&cpu);
  memory_sample (&mem, false);
  disks_sample (&disks, true);
  gpus_sample (&gpus, false);
  net_sample (&net, true);
  procs_sample (&procs);
  sensors_sample (&sensors);
  power_sample (&power);

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
  snprintf (v, sizeof v, _("%d (%d running), %d threads"), procs.n,
            procs.running, procs.threads);
  add_line (&r, _("Processes"), v);

  /* One line per GPU, or why there is none.  */
  for (int i = 0; i < gpus.count; i++)
    {
      const struct gpu_sample *gs = &gpus.dev[i].cur;
      char label[48], temp[32], util[16], vram[64], watts[48];
      fmt_temp (temp, sizeof temp, gs->temp);
      fmt_pct (util, sizeof util, gs->util);
      fmt_used_total (vram, sizeof vram, gs->mem_used, gs->mem_total);
      fmt_of_limit (watts, sizeof watts, gs->power, gs->power_limit, "W");
      snprintf (label, sizeof label, _("GPU %d"), i);
      snprintf (v, sizeof v, "%s, %s, %s, %s, %s",
                gpus.dev[i].name, temp, util, vram, watts);
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

  /* Interfaces with hardware behind them or a routable address; the
     link-local addresses that every interface has are left out.  */
  for (int i = 0; i < net.n; i++)
    {
      const struct net_if *d = &net.dev[i];
      int routable = 0;
      for (int k = 0; k < d->naddr; k++)
        routable += strncmp (d->addr[k], "fe80:", 5) != 0;
      if (streq (d->name, "lo") || (!d->physical && routable == 0))
        continue;
      char label[64];
      snprintf (label, sizeof label, _("Interface %s"), d->name);
      snprintf (v, sizeof v, "%s", d->state[0] ? d->state : "?");
      if (!isnan (d->speed))
        snprintf (v + strlen (v), sizeof v - strlen (v), _(", %g Mb/s"),
                  d->speed);
      for (int k = 0; k < d->naddr; k++)
        if (strncmp (d->addr[k], "fe80:", 5) != 0)
          snprintf (v + strlen (v), sizeof v - strlen (v), ", %s",
                    d->addr[k]);
      add_line (&r, label, v);
    }
  if (net.conns.valid)
    {
      snprintf (v, sizeof v, _("%.0f established, %.0f listening, %.0f UDP"),
                net.conns.established, net.conns.listening, net.conns.udp);
      add_line (&r, _("Connections"), v);
    }

  for (int i = 0; i < power.nbat; i++)
    {
      const struct battery *b = &power.bat[i];
      char label[64];
      snprintf (label, sizeof label, _("Battery %s"), b->name);
      if (isnan (b->capacity))
        snprintf (a1, sizeof a1, "%s", b->level[0] ? b->level : _("n/a"));
      else
        snprintf (a1, sizeof a1, "%.0f%%", b->capacity);
      snprintf (v, sizeof v, "%s, %s, %s", b->model[0] ? b->model : "?", a1,
                b->status[0] ? b->status : "?");
      if (!isnan (b->time_left))
        {
          long m = (long) (b->time_left / 60 + 0.5);
          snprintf (v + strlen (v), sizeof v - strlen (v),
                    _(", %ld:%02ld left"), m / 60, m % 60);
        }
      add_line (&r, label, v);
    }
  for (int i = 0; i < power.nsup; i++)
    {
      char label[64];
      snprintf (label, sizeof label, _("Power supply %s"), power.sup[i].name);
      add_line (&r, label, power.sup[i].online ? _("connected")
                : _("not connected"));
    }

  /* The sensors, and the hottest of them.  */
  if (sensors.n > 0)
    {
      const struct sensor *hot = NULL;
      for (int i = 0; i < sensors.n; i++)
        if (sensors.s[i].kind == SENSOR_TEMP && !isnan (sensors.s[i].value)
            && (!hot || sensors.s[i].value > hot->value))
          hot = &sensors.s[i];
      if (hot)
        {
          fmt_temp (a1, sizeof a1, hot->value);
          snprintf (v, sizeof v, _("%d, hottest %s %s at %s"), sensors.n,
                    hot->chip, hot->label, a1);
        }
      else
        snprintf (v, sizeof v, "%d", sensors.n);
      add_line (&r, _("Sensors"), v);
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
  net_close (&net);
  procs_close (&procs);
  sensors_close (&sensors);
  power_close (&power);
}

int
main (int argc, char **argv)
{
  bool demo = false, once = false;
  double interval = NAN;	/* Unset values come from the settings.  */
  int page = -1, theme = -1;
  const char *log_path = NULL;
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
      case LOG_OPTION:
        log_path = optarg;
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

  /* Settings from the file, then the options given.  */
  struct settings set;
  char config[4096] = "";
  settings_init (&set);
  if (settings_file (config, sizeof config))
    settings_load (&set, config);
  if (!isnan (interval))
    set.interval = interval;
  if (page >= 0)
    set.panels[set.active] = (size_t) page;
  if (theme >= 0)
    set.theme = theme;

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

  static struct app a;
  app_init (&a, src, &set, demo);
  snprintf (a.config_path, sizeof a.config_path, "%s", config);
  if (log_path)
    {
      snprintf (a.log_path, sizeof a.log_path, "%s", log_path);
      int err = log_start (&a.log, log_path, &a);
      if (err)
        error (EXIT_FAILURE, err, _("cannot log to %s"), log_path);
    }
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

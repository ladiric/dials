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
#include "gpu.h"
#include "pages.h"
#include "source.h"
#include "theme.h"
#include "util.h"

enum
{
  ONCE_OPTION = CHAR_MAX + 1,
  HELP_OPTION,
  VERSION_OPTION
};

static const struct option long_options[] = {
  { "demo", no_argument, NULL, 'd' },
  { "interval", required_argument, NULL, 'i' },
  { "page", required_argument, NULL, 'p' },
  { "theme", required_argument, NULL, 't' },
  { "once", no_argument, NULL, ONCE_OPTION },
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

static void
print_line (const char *label, int width, const char *value)
{
  int pad = width - text_width (label);
  printf ("%s:%*s %s\n", label, pad > 0 ? pad : 0, "", value);
}

/* Print a plain-text report, for scripts and for terminals that cannot
   run the full-screen interface.  */
static void
report (struct source *src, bool demo)
{
  static struct gpus gpus;
  struct sys_info in;
  struct sys_sample s;
  struct timespec pause = { 0, 250000000 };
  char kernel[200], up[64], load[64], cpu[32], mem[96], swap[96], used[64];

  source_info (src, &in);
  gpus_open (&gpus, demo, 1);
  nanosleep (&pause, NULL);	/* CPU use needs two readings.  */
  source_sample (src, &s);
  gpus_sample (&gpus, false);

  snprintf (kernel, sizeof kernel, "%s %s", in.kernel, in.arch);
  fmt_uptime (up, sizeof up, s.uptime);
  if (isnan (s.load[0]))
    snprintf (load, sizeof load, "%s", _("n/a"));
  else
    snprintf (load, sizeof load, "%.2f %.2f %.2f",
              s.load[0], s.load[1], s.load[2]);
  if (isnan (s.cpu_pct))
    snprintf (cpu, sizeof cpu, "%s", _("n/a"));
  else
    snprintf (cpu, sizeof cpu, "%.0f%%", s.cpu_pct);
  fmt_used_total (used, sizeof used, s.mem_used, s.mem_total);
  if (s.mem_total > 0)
    snprintf (mem, sizeof mem, "%s (%.0f%%)", used,
              100 * s.mem_used / s.mem_total);
  else
    snprintf (mem, sizeof mem, "%s", used);
  if (s.swap_total > 0)
    {
      fmt_used_total (used, sizeof used, s.swap_used, s.swap_total);
      snprintf (swap, sizeof swap, "%s (%.0f%%)", used,
                100 * s.swap_used / s.swap_total);
    }
  else
    snprintf (swap, sizeof swap, "%s", _("no swap"));

  enum { BASE = 9 };
  const char *labels[BASE + GPU_MAX] = {
    _("Host"), _("System"), _("Kernel"), _("Processor"), _("Uptime"),
    _("Load"), _("CPU"), _("Memory"), _("Swap")
  };
  const char *values[BASE + GPU_MAX] = {
    in.host, in.os, kernel, in.cpu_model, up, load, cpu, mem, swap
  };
  int n = BASE;

  /* One line per GPU, or why there is none.  */
  char glabel[GPU_MAX][32], gvalue[GPU_MAX][256];
  for (int i = 0; i < gpus.count; i++)
    {
      const struct gpu_sample *gs = &gpus.dev[i].cur;
      char temp[32], util[16], vram[64], power[48];
      fmt_temp (temp, sizeof temp, gs->temp);
      fmt_pct (util, sizeof util, gs->util);
      fmt_used_total (vram, sizeof vram, gs->mem_used, gs->mem_total);
      fmt_of_limit (power, sizeof power, gs->power, gs->power_limit, "W");
      snprintf (glabel[i], sizeof glabel[i], _("GPU %d"), i);
      snprintf (gvalue[i], sizeof gvalue[i], "%s, %s, %s, %s, %s",
                gpus.dev[i].name, temp, util, vram, power);
      labels[n] = glabel[i];
      values[n++] = gvalue[i];
    }
  if (gpus.count == 0)
    {
      labels[n] = _("GPU");
      values[n++] = gpus.error;
    }

  int width = 0;
  for (int i = 0; i < n; i++)
    {
      int w = text_width (labels[i]);
      if (w > width)
        width = w;
    }
  for (int i = 0; i < n; i++)
    print_line (labels[i], width, values[i]);
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

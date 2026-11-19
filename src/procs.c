/* procs.c -- the processes running on the system.
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
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "procs.h"
#include "util.h"

static double
rnd (struct procs *s)
{
  s->rng ^= s->rng << 13;
  s->rng ^= s->rng >> 7;
  s->rng ^= s->rng << 17;
  return (double) (s->rng >> 11) / 9007199254740992.0;
}

static int
by_pid (const void *a, const void *b)
{
  const struct proc_info *x = a, *y = b;
  return (x->pid > y->pid) - (x->pid < y->pid);
}

static const struct proc_info *
find (const struct proc_info *list, int n, unsigned int pid)
{
  struct proc_info key;
  key.pid = pid;
  return bsearch (&key, list, n, sizeof *list, by_pid);
}

const struct proc_info *
procs_find (const struct procs *s, unsigned int pid)
{
  return find (s->p, s->n, pid);
}

/* Store the name of user UID in BUF, looking it up once.  */
static void
user_name (struct procs *s, unsigned int uid, char *buf, size_t size)
{
  for (int i = 0; i < s->nusers; i++)
    if (s->users[i].uid == uid)
      {
        snprintf (buf, size, "%s", s->users[i].name);
        return;
      }
  struct passwd pw, *res = NULL;
  char tmp[1024];
  if (getpwuid_r (uid, &pw, tmp, sizeof tmp, &res) == 0 && res)
    str_copy (buf, size, res->pw_name, strlen (res->pw_name));
  else
    snprintf (buf, size, "%u", uid);
  if (s->nusers < (int) ARRAY_CARDINALITY (s->users))
    {
      s->users[s->nusers].uid = uid;
      snprintf (s->users[s->nusers].name, sizeof s->users[0].name, "%s", buf);
      s->nusers++;
    }
}

/* Store the command line of PID in BUF, its arguments separated by
   spaces, or "[COMM]" for a kernel thread, which has none.  */
static void
command_line (unsigned int pid, const char *comm, char *buf, size_t size)
{
  char rel[64];
  snprintf (rel, sizeof rel, "/proc/%u/cmdline", pid);
  int fd = sys_open (rel);
  ssize_t n = read_at0 (fd, buf, size);
  if (fd >= 0)
    close (fd);
  while (n > 0 && buf[n - 1] == '\0')
    n--;
  if (n <= 0)
    {
      snprintf (buf, size, "[%s]", comm);
      return;
    }
  for (ssize_t i = 0; i < n; i++)
    if (buf[i] == '\0' || buf[i] == '\n')
      buf[i] = ' ';
  buf[n] = '\0';
}

static void
add (struct procs *s, const struct proc_info *pi)
{
  if (s->n == s->cap)
    {
      s->cap = s->cap ? 2 * s->cap : 512;
      struct proc_info *p = xmalloc (s->cap * sizeof *p);
      if (s->n)
        memcpy (p, s->p, s->n * sizeof *p);
      free (s->p);
      s->p = p;
    }
  s->p[s->n++] = *pi;
}

/* Read process DIR, a PID, into PI.  Return false if it is gone.  */
static bool
read_proc (struct procs *s, const char *dir, struct proc_info *pi, double dt)
{
  char rel[300], buf[1024];
  snprintf (rel, sizeof rel, "/proc/%s/stat", dir);
  if (!sys_read (rel, buf, sizeof buf))
    return false;

  /* "PID (COMM) STATE PPID ...": the name may hold spaces and
     parentheses, so it ends at the last ')'.  */
  char *open = strchr (buf, '('), *close_paren = strrchr (buf, ')');
  if (!open || !close_paren || close_paren < open || !close_paren[1])
    return false;
  memset (pi, 0, sizeof *pi);
  pi->pid = (unsigned int) atoi (buf);
  str_copy (pi->comm, sizeof pi->comm, open + 1, close_paren - open - 1);
  pi->state = close_paren[2];

  /* Fields from the 4th on, numbered as in proc(5).  */
  unsigned long long f[25] = { 0 };
  char *p = close_paren + 3, *end;
  for (int k = 4; k < 25; k++, p = end)
    {
      long long v = strtoll (p, &end, 10);
      if (end == p)
        break;
      f[k] = (unsigned long long) v;
    }
  pi->ppid = (unsigned int) f[4];
  pi->ticks = f[14] + f[15];
  pi->nice = (int) (long long) f[19];
  pi->threads = (int) f[20];
  pi->start = f[22];
  pi->rss = f[24] * s->page_mib;
  pi->time = (double) pi->ticks / s->hz;

  char full[4096];
  struct stat st;
  snprintf (rel, sizeof rel, "/proc/%s", dir);
  sys_path (full, sizeof full, rel);
  pi->uid = stat (full, &st) == 0 ? st.st_uid : 0;

  /* Keep what does not change from the last reading of the same
     process; a PID can be reused, so its start time must match.  */
  const struct proc_info *old = find (s->prev, s->nprev, pi->pid);
  if (old && old->start == pi->start)
    {
      double d = pi->ticks >= old->ticks ? pi->ticks - old->ticks : 0;
      pi->cpu = dt > 0 ? 100 * d / s->hz / dt : NAN;
      memcpy (pi->cmd, old->cmd, sizeof pi->cmd);
      memcpy (pi->user, old->user, sizeof pi->user);
    }
  else
    {
      pi->cpu = NAN;
      command_line (pi->pid, pi->comm, pi->cmd, sizeof pi->cmd);
      user_name (s, pi->uid, pi->user, sizeof pi->user);
    }
  return true;
}

static void
demo_sample (struct procs *s)
{
  static const struct
  {
    unsigned int pid, ppid;
    const char *user, *comm, *cmd;
    int threads, nice;
    double cpu, rss;
  } demo[] = {
    { 1, 0, "root", "systemd", "/sbin/init", 1, 0, 0.1, 14 },
    { 2, 0, "root", "kthreadd", "[kthreadd]", 1, 0, 0, 0 },
    { 412, 1, "root", "sshd", "sshd: /usr/sbin/sshd -D", 1, 0, 0, 9 },
    { 830, 1, "postgres", "postgres", "/usr/bin/postgres -D /var/lib/pgsql",
      1, 0, 2, 120 },
    { 834, 830, "postgres", "postgres", "postgres: checkpointer", 1, 0, 0.2,
      30 },
    { 1200, 1, "alice", "gnome-shell", "/usr/bin/gnome-shell", 24, 0, 6, 680 },
    { 1500, 1200, "alice", "emacs", "emacs --daemon", 4, 0, 0.5, 210 },
    { 2210, 1200, "alice", "firefox", "/usr/lib64/firefox/firefox", 96, 0,
      18, 1400 },
    { 2240, 2210, "alice", "Web Content", "/usr/lib64/firefox/firefox "
      "-contentproc", 28, 0, 9, 520 },
    { 3001, 1, "alice", "bash", "-bash", 1, 0, 0, 5 },
    { 4242, 3001, "alice", "python3", "python3 train.py --epochs 40", 18, 0,
      310, 2900 },
    { 4300, 3001, "alice", "make", "make -j16", 1, 10, 1, 12 },
    { 4301, 4300, "alice", "cc1", "cc1 -O2 widgets.c", 1, 10, 95, 180 },
  };
  for (size_t i = 0; i < ARRAY_CARDINALITY (demo); i++)
    {
      struct proc_info pi;
      memset (&pi, 0, sizeof pi);
      pi.pid = demo[i].pid;
      pi.ppid = demo[i].ppid;
      pi.threads = demo[i].threads;
      pi.nice = demo[i].nice;
      pi.state = demo[i].cpu > 50 ? 'R' : 'S';
      pi.cpu = demo[i].cpu * (0.7 + rnd (s) * 0.6);
      pi.rss = demo[i].rss;
      pi.time = demo[i].cpu * 7.3 + pi.pid % 97;
      snprintf (pi.user, sizeof pi.user, "%s", demo[i].user);
      snprintf (pi.comm, sizeof pi.comm, "%s", demo[i].comm);
      snprintf (pi.cmd, sizeof pi.cmd, "%s", demo[i].cmd);
      add (s, &pi);
    }
}

void
procs_open (struct procs *s, bool demo)
{
  memset (s, 0, sizeof *s);
  s->demo = demo;
  s->rng = UINT64_C (0x082EFA98EC4E6C89);
  s->hz = sysconf (_SC_CLK_TCK);
  if (s->hz <= 0)
    s->hz = 100;
  s->page_mib = sysconf (_SC_PAGESIZE) / 1048576.0;
  s->prev_time = now_mono ();
}

void
procs_sample (struct procs *s)
{
  double now = now_mono (), dt = now - s->prev_time;
  s->prev_time = now;

  /* The current list becomes the previous one.  */
  struct proc_info *tmp = s->prev;
  int tmpcap = s->prevcap;
  s->prev = s->p;
  s->nprev = s->n;
  s->prevcap = s->cap;
  s->p = tmp;
  s->cap = tmpcap;
  s->n = 0;
  s->running = s->sleeping = s->waiting = s->threads = 0;

  if (s->demo)
    demo_sample (s);
  else
    {
      char path[4096];
      sys_path (path, sizeof path, "/proc");
      DIR *dir = opendir (path);
      if (dir)
        {
          struct dirent *e;
          while ((e = readdir (dir)))
            {
              struct proc_info pi;
              if (e->d_name[0] >= '1' && e->d_name[0] <= '9'
                  && read_proc (s, e->d_name, &pi, dt))
                add (s, &pi);
            }
          closedir (dir);
        }
    }
  qsort (s->p, s->n, sizeof *s->p, by_pid);

  for (int i = 0; i < s->n; i++)
    {
      s->threads += s->p[i].threads;
      switch (s->p[i].state)
        {
        case 'R':
          s->running++;
          break;
        case 'D':
          s->waiting++;
          break;
        default:
          s->sleeping++;
          break;
        }
    }
  s->valid = true;
}

void
procs_close (struct procs *s)
{
  free (s->p);
  free (s->prev);
}

/* Acting on real processes is refused for simulated data and for a copy
   of /proc, where the PIDs mean nothing.  */
static int
refuse (const struct procs *s)
{
  if (s->demo)
    return -1;
  if (sysroot_set ())
    return ENOTSUP;
  return 0;
}

int
procs_signal (struct procs *s, unsigned int pid, int sig)
{
  int r = refuse (s);
  if (r)
    return r;
  return kill ((pid_t) pid, sig) == 0 ? 0 : errno;
}

int
procs_renice (struct procs *s, unsigned int pid, int nice)
{
  int r = refuse (s);
  if (r)
    return r;
  return setpriority (PRIO_PROCESS, pid, nice) == 0 ? 0 : errno;
}

/* gpu_nvml.c -- NVIDIA GPUs through the NVML library.
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

#include <dlfcn.h>
#include <stdio.h>

#include "gpu.h"
#include "util.h"

/* NVML comes with the NVIDIA driver.  Dials loads it at run time if it
   is there, so building needs neither the library nor its header; the
   few declarations below follow NVIDIA's published API.  */

typedef int nvml_return;
typedef struct nvml_device_st *nvml_device;

enum
{
  NVML_SUCCESS = 0,
  NVML_ERROR_INSUFFICIENT_SIZE = 7,
  NVML_TEMPERATURE_GPU = 0,
  NVML_CLOCK_SM = 1,
  NVML_CLOCK_MEM = 2
};

/* Used memory is this when NVML cannot tell, as in some containers.  */
#define NVML_VALUE_NOT_AVAILABLE (~0ULL)

struct nvml_utilization
{
  unsigned int gpu, memory;
};

struct nvml_memory
{
  unsigned long long total, free, used;
};

/* nvmlProcessInfo_t, as filled by the _v2 and _v3 functions.  */
struct nvml_process
{
  unsigned int pid;
  unsigned long long used_memory;
  unsigned int gpu_instance, compute_instance;
};

/* The same, as filled by the original functions.  */
struct nvml_process_v1
{
  unsigned int pid;
  unsigned long long used_memory;
};

struct nvml_process_util
{
  unsigned int pid;
  unsigned long long timestamp;
  unsigned int sm, memory, encoder, decoder;
};

struct nvml
{
  void *lib;
  nvml_return (*shutdown) (void);
  nvml_return (*name) (nvml_device, char *, unsigned int);
  nvml_return (*temperature) (nvml_device, int, unsigned int *);
  nvml_return (*utilization) (nvml_device, struct nvml_utilization *);
  nvml_return (*memory) (nvml_device, struct nvml_memory *);
  nvml_return (*power) (nvml_device, unsigned int *);
  nvml_return (*power_limit) (nvml_device, unsigned int *);
  nvml_return (*clock) (nvml_device, int, unsigned int *);
  nvml_return (*max_clock) (nvml_device, int, unsigned int *);
  nvml_return (*fan) (nvml_device, unsigned int *);
  nvml_return (*encoder) (nvml_device, unsigned int *, unsigned int *);
  nvml_return (*decoder) (nvml_device, unsigned int *, unsigned int *);
  nvml_return (*pstate) (nvml_device, int *);
  nvml_return (*compute_procs) (nvml_device, unsigned int *, void *);
  nvml_return (*graphics_procs) (nvml_device, unsigned int *, void *);
  bool procs_v1;		/* The process functions are the originals.  */
  nvml_return (*proc_util) (nvml_device, struct nvml_process_util *,
                            unsigned int *, unsigned long long);

  int count;
  nvml_device dev[GPU_MAX];
  char version[96];		/* Driver version.  */
  /* Values that do not change, read once.  */
  double limit_w[GPU_MAX], clock_sm_max[GPU_MAX], clock_mem_max[GPU_MAX];
  /* Per GPU: the newest process sample seen, and the last known use
     of each process, kept for when NVML has no new sample.  */
  unsigned long long last_seen[GPU_MAX];
  struct
  {
    unsigned int pid;
    double sm;
  } sm_cache[GPU_MAX][GPU_PROC_MAX];
  int sm_cached[GPU_MAX];
  struct nvml_process_util *util;	/* Buffer for process samples.  */
  unsigned int util_cap;
};

/* Return the first of the symbols NAMES that the library has.  */
static void *
find (void *lib, const char *const *names)
{
  for (; *names; names++)
    {
      void *p = dlsym (lib, *names);
      if (p)
        return p;
    }
  return NULL;
}

#define FIND(...) find (n->lib, (const char *const[]) { __VA_ARGS__, NULL })

static bool
nvml_open (void **priv, char *err, size_t esize)
{
  struct nvml *n = xcalloc (1, sizeof *n);

  n->lib = dlopen ("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!n->lib)
    {
      snprintf (err, esize, "%s",
                _("the NVIDIA driver library (libnvidia-ml.so.1) "
                  "is not installed"));
      free (n);
      return false;
    }

  nvml_return (*init) (void) = FIND ("nvmlInit_v2", "nvmlInit");
  nvml_return (*get_count) (unsigned int *)
    = FIND ("nvmlDeviceGetCount_v2", "nvmlDeviceGetCount");
  nvml_return (*get_handle) (unsigned int, nvml_device *)
    = FIND ("nvmlDeviceGetHandleByIndex_v2", "nvmlDeviceGetHandleByIndex");
  nvml_return (*get_driver) (char *, unsigned int)
    = FIND ("nvmlSystemGetDriverVersion");
  const char *(*error_string) (nvml_return) = FIND ("nvmlErrorString");

  n->shutdown = FIND ("nvmlShutdown");
  n->name = FIND ("nvmlDeviceGetName");
  n->temperature = FIND ("nvmlDeviceGetTemperature");
  n->utilization = FIND ("nvmlDeviceGetUtilizationRates");
  n->memory = FIND ("nvmlDeviceGetMemoryInfo");
  n->power = FIND ("nvmlDeviceGetPowerUsage");
  n->power_limit = FIND ("nvmlDeviceGetEnforcedPowerLimit");
  n->clock = FIND ("nvmlDeviceGetClockInfo");
  n->max_clock = FIND ("nvmlDeviceGetMaxClockInfo");
  n->fan = FIND ("nvmlDeviceGetFanSpeed");
  n->encoder = FIND ("nvmlDeviceGetEncoderUtilization");
  n->decoder = FIND ("nvmlDeviceGetDecoderUtilization");
  n->pstate = FIND ("nvmlDeviceGetPerformanceState");
  n->proc_util = FIND ("nvmlDeviceGetProcessUtilization");
  n->compute_procs = FIND ("nvmlDeviceGetComputeRunningProcesses_v3",
                           "nvmlDeviceGetComputeRunningProcesses_v2");
  n->graphics_procs = FIND ("nvmlDeviceGetGraphicsRunningProcesses_v3",
                            "nvmlDeviceGetGraphicsRunningProcesses_v2");
  if (!n->compute_procs && !n->graphics_procs)
    {
      n->procs_v1 = true;
      n->compute_procs = FIND ("nvmlDeviceGetComputeRunningProcesses");
      n->graphics_procs = FIND ("nvmlDeviceGetGraphicsRunningProcesses");
    }

  if (!init || !get_count || !get_handle)
    {
      snprintf (err, esize, "%s",
                _("the NVIDIA driver library is too old to use"));
      dlclose (n->lib);
      free (n);
      return false;
    }

  nvml_return rc = init ();
  if (rc != NVML_SUCCESS)
    {
      snprintf (err, esize, _("the NVIDIA driver did not respond: %s"),
                error_string ? error_string (rc) : "?");
      dlclose (n->lib);
      free (n);
      return false;
    }

  unsigned int count = 0;
  if (get_count (&count) != NVML_SUCCESS)
    count = 0;
  for (unsigned int i = 0; i < count && n->count < GPU_MAX; i++)
    if (get_handle (i, &n->dev[n->count]) == NVML_SUCCESS)
      n->count++;

  for (int i = 0; i < n->count; i++)
    {
      unsigned int v;
      nvml_device d = n->dev[i];
      n->limit_w[i] = n->clock_sm_max[i] = n->clock_mem_max[i] = NAN;
      if (n->power_limit && n->power_limit (d, &v) == NVML_SUCCESS)
        n->limit_w[i] = v / 1000.0;
      if (n->max_clock && n->max_clock (d, NVML_CLOCK_SM, &v) == NVML_SUCCESS)
        n->clock_sm_max[i] = v;
      if (n->max_clock && n->max_clock (d, NVML_CLOCK_MEM, &v) == NVML_SUCCESS)
        n->clock_mem_max[i] = v;
    }

  char version[80];
  if (get_driver && get_driver (version, sizeof version) == NVML_SUCCESS)
    snprintf (n->version, sizeof n->version, "NVIDIA %s", version);
  else
    snprintf (n->version, sizeof n->version, "%s", "NVIDIA");

  *priv = n;
  return true;
}

static int
nvml_count (void *priv)
{
  struct nvml *n = priv;
  return n->count;
}

static void
nvml_describe (void *priv, int i, char *buf, size_t size, char *driver,
               size_t dsize)
{
  struct nvml *n = priv;
  char name[96];
  if (n->name && n->name (n->dev[i], name, sizeof name) == NVML_SUCCESS)
    snprintf (buf, size, "%s", name);
  else
    snprintf (buf, size, "%s", "NVIDIA GPU");
  snprintf (driver, dsize, "%s", n->version);
}

/* Store the command name of process PID in BUF.  */
static void
process_name (unsigned int pid, char *buf, size_t size)
{
  char path[64];
  snprintf (path, sizeof path, "/proc/%u/comm", pid);
  if (read_file (path, buf, size))
    buf[strcspn (buf, "\n")] = '\0';
  else
    snprintf (buf, size, "%s", "?");
}

/* Add COUNT processes from BUF, in the layout V1 tells, to S.  */
static void
add_procs (struct gpu_sample *s, const void *buf, unsigned int count,
           bool v1, bool compute)
{
  for (unsigned int k = 0; k < count; k++)
    {
      unsigned int pid;
      unsigned long long used;
      if (v1)
        {
          const struct nvml_process_v1 *p
            = (const struct nvml_process_v1 *) buf + k;
          pid = p->pid;
          used = p->used_memory;
        }
      else
        {
          const struct nvml_process *p = (const struct nvml_process *) buf + k;
          pid = p->pid;
          used = p->used_memory;
        }
      double mem = used == NVML_VALUE_NOT_AVAILABLE ? NAN : used / 1048576.0;

      /* A process can do both compute and graphics work.  */
      struct gpu_proc *gp = NULL;
      for (int j = 0; j < s->nproc; j++)
        if (s->proc[j].pid == pid)
          gp = &s->proc[j];
      if (!gp)
        {
          if (s->nproc >= GPU_PROC_MAX)
            continue;
          gp = &s->proc[s->nproc++];
          memset (gp, 0, sizeof *gp);
          gp->pid = pid;
          gp->mem = mem;
          gp->sm = NAN;
          process_name (pid, gp->name, sizeof gp->name);
        }
      else if (!isnan (mem) && !(gp->mem >= mem))
        gp->mem = mem;
      if (compute)
        gp->compute = true;
      else
        gp->graphics = true;
    }
}

static int
by_memory (const void *a, const void *b)
{
  const struct gpu_proc *p = a, *q = b;
  double x = isnan (p->mem) ? -1 : p->mem, y = isnan (q->mem) ? -1 : q->mem;
  return (x < y) - (x > y);
}

static void
read_procs (struct nvml *n, int i, struct gpu_sample *s)
{
  union
  {
    struct nvml_process v3[GPU_PROC_MAX];
    struct nvml_process_v1 v1[GPU_PROC_MAX];
  } buf;
  unsigned int count;

  count = GPU_PROC_MAX;
  if (n->compute_procs
      && n->compute_procs (n->dev[i], &count, &buf) == NVML_SUCCESS)
    add_procs (s, &buf, count, n->procs_v1, true);
  count = GPU_PROC_MAX;
  if (n->graphics_procs
      && n->graphics_procs (n->dev[i], &count, &buf) == NVML_SUCCESS)
    add_procs (s, &buf, count, n->procs_v1, false);

  /* Each process's share of the GPU, from samples newer than the ones
     already seen; the newest sample of each process wins.  NVML first
     tells how many samples it holds, then fills a buffer that big.  */
  unsigned int nu = 0;
  bool fresh = false;		/* Whether NVML had new samples.  */
  nvml_return rc = NVML_SUCCESS;
  if (n->proc_util && s->nproc > 0)
    rc = n->proc_util (n->dev[i], NULL, &nu, n->last_seen[i]);
  if ((rc == NVML_SUCCESS || rc == NVML_ERROR_INSUFFICIENT_SIZE) && nu > 0)
    {
      if (nu > n->util_cap)
        {
          free (n->util);
          n->util = xmalloc (nu * sizeof *n->util);
          n->util_cap = nu;
        }
      if (n->proc_util (n->dev[i], n->util, &nu, n->last_seen[i])
          == NVML_SUCCESS)
        {
          unsigned long long newest = n->last_seen[i];
          unsigned long long when[GPU_PROC_MAX] = { 0 };
          for (unsigned int k = 0; k < nu && k < n->util_cap; k++)
            {
              const struct nvml_process_util *u = &n->util[k];
              if (u->timestamp > newest)
                newest = u->timestamp;
              for (int j = 0; j < s->nproc; j++)
                if (s->proc[j].pid == u->pid && u->timestamp >= when[j])
                  {
                    when[j] = u->timestamp;
                    s->proc[j].sm = u->sm;
                  }
            }
          n->last_seen[i] = newest;
          fresh = nu > 0;
        }
    }

  /* A process with no new sample was idle if others had samples.  If
     NVML had none at all, keep the last known values.  Then remember
     this round.  */
  for (int j = 0; j < s->nproc; j++)
    if (isnan (s->proc[j].sm))
      {
        if (fresh)
          s->proc[j].sm = 0;
        else
          for (int c = 0; c < n->sm_cached[i]; c++)
            if (n->sm_cache[i][c].pid == s->proc[j].pid)
              s->proc[j].sm = n->sm_cache[i][c].sm;
      }
  n->sm_cached[i] = 0;
  for (int j = 0; j < s->nproc; j++)
    if (!isnan (s->proc[j].sm))
      {
        int c = n->sm_cached[i]++;
        n->sm_cache[i][c].pid = s->proc[j].pid;
        n->sm_cache[i][c].sm = s->proc[j].sm;
      }

  qsort (s->proc, s->nproc, sizeof s->proc[0], by_memory);
}

/* Each NVML call takes a little while, so only what the history and
   the Overview page need is read every time; the rest only while the
   GPU page shows it.  */
static void
nvml_sample (void *priv, int i, bool details, struct gpu_sample *s)
{
  struct nvml *n = priv;
  nvml_device d = n->dev[i];
  unsigned int v, period;
  int ps;
  struct nvml_utilization u;
  struct nvml_memory m;

  if (n->temperature && n->temperature (d, NVML_TEMPERATURE_GPU, &v) == 0)
    s->temp = v;
  if (n->utilization && n->utilization (d, &u) == NVML_SUCCESS)
    {
      s->util = u.gpu;
      s->mem_util = u.memory;
    }
  if (n->memory && n->memory (d, &m) == NVML_SUCCESS)
    {
      s->mem_total = m.total / 1048576.0;
      s->mem_used = m.used / 1048576.0;
    }
  if (n->power && n->power (d, &v) == NVML_SUCCESS)
    s->power = v / 1000.0;
  s->power_limit = n->limit_w[i];
  s->clock_sm_max = n->clock_sm_max[i];
  s->clock_mem_max = n->clock_mem_max[i];
  if (!details)
    return;

  if (n->clock && n->clock (d, NVML_CLOCK_SM, &v) == NVML_SUCCESS)
    s->clock_sm = v;
  if (n->clock && n->clock (d, NVML_CLOCK_MEM, &v) == NVML_SUCCESS)
    s->clock_mem = v;
  if (n->fan && n->fan (d, &v) == NVML_SUCCESS)
    s->fan = v;
  if (n->encoder && n->encoder (d, &v, &period) == NVML_SUCCESS)
    s->enc = v;
  if (n->decoder && n->decoder (d, &v, &period) == NVML_SUCCESS)
    s->dec = v;
  if (n->pstate && n->pstate (d, &ps) == NVML_SUCCESS && ps >= 0 && ps <= 15)
    s->pstate = ps;
  s->procs_known = n->compute_procs || n->graphics_procs;
  read_procs (n, i, s);
}

static void
nvml_close (void *priv)
{
  struct nvml *n = priv;
  if (n->shutdown)
    n->shutdown ();
  dlclose (n->lib);
  free (n->util);
  free (n);
}

const struct gpu_backend gpu_nvml_backend = {
  nvml_open, nvml_count, nvml_describe, nvml_sample, nvml_close
};

/* gpu.h -- graphics processors and where their readings come from.
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

#ifndef DIALS_GPU_H
#define DIALS_GPU_H

#include <stdbool.h>
#include <stddef.h>

#include "metric.h"

enum
{
  GPU_MAX = 8,			/* GPUs shown.  */
  GPU_PROC_MAX = 32,		/* Processes listed per GPU.  */
  GPU_TEMP_ALERT = 85,		/* Degrees Celsius.  */
  GPU_MEM_ALERT = 95		/* Percent of video memory used.  */
};

/* A process using a GPU.  */
struct gpu_proc
{
  unsigned int pid;
  bool compute, graphics;
  double mem;			/* MiB, or NaN.  */
  double sm;			/* Percent of the GPU's time, or NaN.  */
  char name[64];
};

/* One reading of one GPU.  Unknown values are NaN.  */
struct gpu_sample
{
  double temp;			/* Degrees Celsius.  */
  double util, mem_util, enc, dec, fan;	/* Percentages.  */
  double mem_used, mem_total;	/* MiB.  */
  double power, power_limit;	/* Watts.  */
  double clock_sm, clock_sm_max, clock_mem, clock_mem_max;	/* MHz.  */
  int pstate;			/* Performance state 0-15, or -1.  */
  bool procs_known;		/* Whether the driver lists processes.  */
  int nproc;
  struct gpu_proc proc[GPU_PROC_MAX];
};

/* A source of GPUs: NVIDIA's NVML library, the kernel's DRM drivers
   for AMD and Intel, or simulated ones.  */
struct gpu_backend
{
  /* Start up.  On failure, describe why in ERR and return false.  */
  bool (*open) (void **priv, char *err, size_t esize);
  int (*count) (void *priv);
  /* Store the name of GPU I and a description of its driver.  */
  void (*describe) (void *priv, int i, char *name, size_t nsize,
                    char *driver, size_t dsize);
  /* Read GPU I into OUT, which is cleared first.  If DETAILS, read
     everything the GPU page shows, its processes included; else only
     temperature, utilization, memory and power.  */
  void (*sample) (void *priv, int i, bool details, struct gpu_sample *out);
  void (*close) (void *priv);
};

extern const struct gpu_backend gpu_nvml_backend;
extern const struct gpu_backend gpu_drm_backend;
extern const struct gpu_backend gpu_demo_backend;

enum { GPU_BACKENDS = 2 };

struct gpu_dev
{
  char name[96];
  char driver[64];
  int backend;			/* Index into gpus.backends.  */
  int index;			/* The GPU's number in its backend.  */
  struct gpu_sample cur;
  struct metric temp, util, mem, power;	/* History, in the units above.  */
};

struct gpus
{
  int nbackends;
  struct
  {
    const struct gpu_backend *be;
    void *priv;
  } backends[GPU_BACKENDS];
  int count;
  bool procs_valid;		/* Whether the last sample read details.  */
  char error[160];		/* Why there are no GPUs, if there are none.  */
  struct gpu_dev dev[GPU_MAX];
};

void gpus_open (struct gpus *g, bool demo, size_t history);
void gpus_sample (struct gpus *g, bool details);
void gpus_clear_history (struct gpus *g);
void gpus_close (struct gpus *g);
void gpu_sample_clear (struct gpu_sample *s);

#endif /* DIALS_GPU_H */

/* net.c -- network interfaces and connections.
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

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "net.h"
#include "util.h"

static double
rnd (struct net *n)
{
  n->rng ^= n->rng << 13;
  n->rng ^= n->rng >> 7;
  n->rng ^= n->rng << 17;
  return (double) (n->rng >> 11) / 9007199254740992.0;
}

/* Physical interfaces first, then the others; the loopback last.  */
static int
by_kind_and_name (const void *a, const void *b)
{
  const struct net_if *x = a, *y = b;
  bool xlo = strcmp (x->name, "lo") == 0, ylo = strcmp (y->name, "lo") == 0;
  if (xlo != ylo)
    return xlo - ylo;
  if (x->physical != y->physical)
    return y->physical - x->physical;
  return strcmp (x->name, y->name);
}

static void
clear_readings (struct net_if *d)
{
  d->rx_bps = d->tx_bps = d->rx_pps = d->tx_pps = NAN;
  d->errors = d->drops = d->rx_total = d->tx_total = NAN;
}

static struct net_if *
find_or_add (struct net *n, const char *name)
{
  for (int i = 0; i < n->n; i++)
    if (strcmp (n->dev[i].name, name) == 0)
      return &n->dev[i];
  if (n->n >= NET_MAX)
    return NULL;

  struct net_if *d = &n->dev[n->n++];
  memset (d, 0, sizeof *d);
  str_copy (d->name, sizeof d->name, name, strlen (name));
  d->speed = d->mtu = NAN;
  clear_readings (d);
  metric_init (&d->m_rx, n->history);
  metric_init (&d->m_tx, n->history);
  if (n->demo)
    d->physical = strcmp (name, "lo") != 0;
  else
    {
      /* Hardware interfaces have a device behind them.  */
      char rel[128], path[4096];
      struct stat st;
      snprintf (rel, sizeof rel, "/sys/class/net/%s/device", name);
      sys_path (path, sizeof path, rel);
      d->physical = stat (path, &st) == 0;
    }
  return d;
}

/* Bytes, packets, errors and drops received, then the same sent.  The
   receive and transmit groups in /proc/net/dev are 8 fields apart.  */
static const int dev_fields[NET_COUNTERS] = { 0, 1, 2, 3, 8, 9, 10, 11 };

static void
use_counters (struct net_if *d, const unsigned long long *v, double dt)
{
  d->rx_total = (double) v[0];
  d->tx_total = (double) v[4];
  if (d->have_prev && dt > 0)
    {
      double r[NET_COUNTERS];
      for (int k = 0; k < NET_COUNTERS; k++)
        r[k] = (v[k] >= d->prev[k] ? (double) (v[k] - d->prev[k]) : 0) / dt;
      d->rx_bps = r[0];
      d->rx_pps = r[1];
      d->tx_bps = r[4];
      d->tx_pps = r[5];
      d->errors = r[2] + r[6];
      d->drops = r[3] + r[7];
    }
  memcpy (d->prev, v, sizeof d->prev);
  d->have_prev = true;
}

static void
read_dev (struct net *n, double dt)
{
  if (read_at0 (n->fd_dev, n->buf, sizeof n->buf) <= 0)
    return;
  for (char *line = n->buf; line && *line;)
    {
      char *next = strchr (line, '\n');
      if (next)
        *next++ = '\0';
      char *colon = strchr (line, ':');
      if (colon)
        {
          *colon = '\0';
          char *name = line + strspn (line, " ");
          struct net_if *d = find_or_add (n, name);
          if (d)
            {
              unsigned long long all[16] = { 0 }, v[NET_COUNTERS];
              char *s = colon + 1, *end;
              for (int k = 0; k < 16; k++, s = end)
                {
                  all[k] = strtoull (s, &end, 10);
                  if (end == s)
                    break;
                }
              for (int k = 0; k < NET_COUNTERS; k++)
                v[k] = all[dev_fields[k]];
              d->seen = true;
              use_counters (d, v, dt);
            }
        }
      line = next;
    }
}

static void
read_link (struct net_if *d)
{
  char rel[128], buf[64];
  double v;

  snprintf (rel, sizeof rel, "/sys/class/net/%s/operstate", d->name);
  if (sys_read (rel, buf, sizeof buf))
    {
      buf[strcspn (buf, "\n")] = '\0';
      str_copy (d->state, sizeof d->state, buf, strlen (buf));
    }
  /* Virtual interfaces report no speed, or -1.  */
  snprintf (rel, sizeof rel, "/sys/class/net/%s/speed", d->name);
  d->speed = sys_number (rel, &v) && v > 0 ? v : NAN;
  snprintf (rel, sizeof rel, "/sys/class/net/%s/mtu", d->name);
  d->mtu = sys_number (rel, &v) ? v : NAN;
  snprintf (rel, sizeof rel, "/sys/class/net/%s/address", d->name);
  if (sys_read (rel, buf, sizeof buf))
    {
      buf[strcspn (buf, "\n")] = '\0';
      str_copy (d->mac, sizeof d->mac, buf, strlen (buf));
    }
}

static int
prefix_length (const struct sockaddr *mask)
{
  const unsigned char *b;
  size_t len;
  if (!mask)
    return 0;
  if (mask->sa_family == AF_INET)
    {
      b = (const unsigned char *) &((const struct sockaddr_in *) mask)->sin_addr;
      len = 4;
    }
  else
    {
      b = (const unsigned char *) &((const struct sockaddr_in6 *) mask)->sin6_addr;
      len = 16;
    }
  int bits = 0;
  for (size_t i = 0; i < len; i++)
    for (int k = 7; k >= 0; k--)
      bits += (b[i] >> k) & 1;
  return bits;
}

/* The addresses of each interface.  They do not come from a file, so
   they are not read from a copy of /proc.  */
static void
read_addresses (struct net *n)
{
  struct ifaddrs *list;

  for (int i = 0; i < n->n; i++)
    n->dev[i].naddr = 0;
  if (sysroot_set () || getifaddrs (&list) != 0)
    return;
  for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next)
    {
      if (!ifa->ifa_addr || (ifa->ifa_addr->sa_family != AF_INET
                             && ifa->ifa_addr->sa_family != AF_INET6))
        continue;
      struct net_if *d = NULL;
      for (int i = 0; i < n->n; i++)
        if (strcmp (n->dev[i].name, ifa->ifa_name) == 0)
          d = &n->dev[i];
      if (!d || d->naddr >= NET_ADDR_MAX)
        continue;
      char text[INET6_ADDRSTRLEN];
      const void *a = ifa->ifa_addr->sa_family == AF_INET
        ? (const void *) &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr
        : (const void *) &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
      if (!inet_ntop (ifa->ifa_addr->sa_family, a, text, sizeof text))
        continue;
      snprintf (d->addr[d->naddr++], sizeof d->addr[0], "%s/%d", text,
                prefix_length (ifa->ifa_netmask));
    }
  freeifaddrs (list);
}

/* Count the sockets in a /proc/net/tcp-style file by state, the fourth
   field, in hexadecimal.  */
static void
count_sockets (const char *path, double *by_state)
{
  char full[4096], line[512];
  sys_path (full, sizeof full, path);
  FILE *f = fopen (full, "re");
  if (!f)
    return;
  if (fgets (line, sizeof line, f))	/* The heading.  */
    while (fgets (line, sizeof line, f))
      {
        char st[8];
        if (sscanf (line, "%*s %*s %*s %7s", st) == 1)
          {
            unsigned long k = strtoul (st, NULL, 16);
            if (k < 16)
              by_state[k]++;
          }
      }
  fclose (f);
}

static void
read_conns (struct net_conns *c)
{
  double tcp[16] = { 0 }, udp[16] = { 0 };
  count_sockets ("/proc/net/tcp", tcp);
  count_sockets ("/proc/net/tcp6", tcp);
  count_sockets ("/proc/net/udp", udp);
  count_sockets ("/proc/net/udp6", udp);

  /* The kernel's TCP states: 1 established, 2 SYN sent, 3 SYN
     received, 4 and 5 FIN wait, 6 TIME-WAIT, 7 closed, 8 CLOSE-WAIT,
     9 last ACK, 10 listening, 11 closing.  */
  c->established = tcp[1];
  c->opening = tcp[2] + tcp[3];
  c->closing = tcp[4] + tcp[5] + tcp[7] + tcp[9] + tcp[11];
  c->time_wait = tcp[6];
  c->close_wait = tcp[8];
  c->listening = tcp[10];
  c->udp = 0;
  for (int k = 0; k < 16; k++)
    c->udp += udp[k];
  c->valid = true;
}

static void
demo_sample (struct net *n, bool details, double dt)
{
  static const struct
  {
    const char *name, *state, *mac, *addr;
    double speed, scale;
  } ifs[] = {
    { "eth0", "up", "52:54:00:12:34:56", "192.168.1.20/24", 1000, 4e6 },
    { "wlan0", "down", "52:54:00:ab:cd:ef", NULL, NAN, 0 },
    { "lo", "unknown", "00:00:00:00:00:00", "127.0.0.1/8", NAN, 2e4 },
  };
  for (size_t i = 0; i < ARRAY_CARDINALITY (ifs); i++)
    {
      struct net_if *d = find_or_add (n, ifs[i].name);
      if (!d)
        continue;
      d->seen = true;
      double burst = rnd (n) < 0.15 ? 20 : 1;
      unsigned long long v[NET_COUNTERS];
      memcpy (v, d->prev, sizeof v);
      v[0] += (unsigned long long) (ifs[i].scale * burst * rnd (n) * dt);
      v[4] += (unsigned long long) (ifs[i].scale * 0.3 * rnd (n) * dt);
      v[1] += v[0] / 1400 - d->prev[0] / 1400;
      v[5] += v[4] / 1400 - d->prev[4] / 1400;
      use_counters (d, v, dt);
      if (details)
        {
          snprintf (d->state, sizeof d->state, "%s", ifs[i].state);
          snprintf (d->mac, sizeof d->mac, "%s", ifs[i].mac);
          d->speed = ifs[i].speed;
          d->mtu = strcmp (ifs[i].name, "lo") == 0 ? 65536 : 1500;
          d->naddr = 0;
          if (ifs[i].addr)
            snprintf (d->addr[d->naddr++], sizeof d->addr[0], "%s",
                      ifs[i].addr);
        }
    }
  if (details)
    n->conns = (struct net_conns) { true, 42 + rnd (n) * 6, 12, 9, 1, 0, 1,
                                    14 };
}

void
net_open (struct net *n, bool demo, size_t history)
{
  memset (n, 0, sizeof *n);
  n->demo = demo;
  n->history = history;
  n->rng = UINT64_C (0x243F6A8885A308D3);
  n->fd_dev = demo ? -1 : sys_open ("/proc/net/dev");
  n->prev_time = now_mono ();
  net_sample (n, true);		/* Rates need a first reading.  */
  net_clear_history (n);
}

void
net_sample (struct net *n, bool details)
{
  double now = now_mono (), dt = now - n->prev_time;
  n->prev_time = now;

  for (int i = 0; i < n->n; i++)
    {
      n->dev[i].seen = false;
      clear_readings (&n->dev[i]);
    }
  if (n->demo)
    demo_sample (n, details, dt);
  else
    read_dev (n, dt);

  /* Forget interfaces that went away.  */
  int kept = 0;
  for (int i = 0; i < n->n; i++)
    if (n->dev[i].seen)
      n->dev[kept++] = n->dev[i];
    else
      {
        metric_free (&n->dev[i].m_rx);
        metric_free (&n->dev[i].m_tx);
      }
  n->n = kept;
  qsort (n->dev, n->n, sizeof n->dev[0], by_kind_and_name);

  if (details && !n->demo)
    {
      for (int i = 0; i < n->n; i++)
        read_link (&n->dev[i]);
      read_addresses (n);
      read_conns (&n->conns);
    }
  for (int i = 0; i < n->n; i++)
    {
      metric_push (&n->dev[i].m_rx, n->dev[i].rx_bps / 1048576);
      metric_push (&n->dev[i].m_tx, n->dev[i].tx_bps / 1048576);
    }
}

void
net_clear_history (struct net *n)
{
  for (int i = 0; i < n->n; i++)
    {
      metric_clear (&n->dev[i].m_rx);
      metric_clear (&n->dev[i].m_tx);
    }
}

void
net_close (struct net *n)
{
  for (int i = 0; i < n->n; i++)
    {
      metric_free (&n->dev[i].m_rx);
      metric_free (&n->dev[i].m_tx);
    }
  if (n->fd_dev >= 0)
    close (n->fd_dev);
}

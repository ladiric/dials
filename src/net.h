/* net.h -- network interfaces and connections.
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

#ifndef DIALS_NET_H
#define DIALS_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "metric.h"

enum
{
  NET_MAX = 64,			/* Interfaces shown.  */
  NET_ADDR_MAX = 4,		/* Addresses shown per interface.  */
  NET_COUNTERS = 8		/* Fields of /proc/net/dev used.  */
};

/* An interface.  Readings are NaN when unknown.  */
struct net_if
{
  char name[32];
  bool physical;		/* Backed by hardware, not virtual.  */
  char state[16];		/* "up", "down", ...  */
  double speed;			/* Link speed, Mb/s.  */
  double mtu;
  char mac[24];
  int naddr;
  char addr[NET_ADDR_MAX][64];	/* With prefix length, "10.0.0.2/24".  */
  double rx_bps, tx_bps;	/* Bytes per second.  */
  double rx_pps, tx_pps;	/* Packets per second.  */
  double errors, drops;		/* Per second, both directions.  */
  double rx_total, tx_total;	/* Bytes since the interface came up.  */
  struct metric m_rx, m_tx;	/* MiB/s.  */

  /* Private.  */
  bool seen;
  bool have_prev;
  unsigned long long prev[NET_COUNTERS];
};

/* TCP connections by state, and UDP sockets.  */
struct net_conns
{
  bool valid;
  double established, listening, time_wait, close_wait, opening, closing;
  double udp;
};

struct net
{
  int n;
  struct net_if dev[NET_MAX];	/* Physical first, then by name.  */
  struct net_conns conns;

  /* Private.  */
  bool demo;
  uint64_t rng;
  size_t history;
  int fd_dev;
  double prev_time;
  char buf[65536];
};

void net_open (struct net *n, bool demo, size_t history);

/* Take a reading; read link details, addresses and connections too if
   DETAILS.  */
void net_sample (struct net *n, bool details);
void net_clear_history (struct net *n);
void net_close (struct net *n);

#endif /* DIALS_NET_H */

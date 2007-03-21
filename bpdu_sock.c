/*****************************************************************************
  Copyright (c) 2006 EMC Corporation.

  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Authors: Srinivas Aji <Aji_Srinivas@emc.com>

******************************************************************************/

#include "bpdu_sock.h"
#include "epoll_loop.h"
#include "netif_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/llc.h>

#include "log.h"

#ifndef AF_LLC
#define AF_LLC 26
#endif

static const uint8_t stp_mc[ETH_ALEN] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };

void bpdu_send(struct epoll_event_handler *h, unsigned char *data, int len)
{
  struct sockaddr_llc to;
  memset(&to, 0, sizeof(to));
  to.sllc_family = AF_LLC;
  to.sllc_arphrd = ARPHRD_ETHER;
  to.sllc_sap = LLC_SAP_BSPAN;
  memcpy(to.sllc_mac, stp_mc, ETH_ALEN);

  if (fcntl(h->fd, F_SETFL, 0) < 0)
    ERROR("Error unsetting O_NONBLOCK: %m");

  int l = sendto(h->fd, data, len, 0, (struct sockaddr *)&to, sizeof(to));
  if (l < 0)
    ERROR("sendto failed: %m");
  else if (l != len)
    ERROR("short write in sendto: %d instead of %d", l, len);

  if (fcntl(h->fd, F_SETFL, O_NONBLOCK) < 0)
    ERROR("Error setting O_NONBLOCK: %m");
}

void bpdu_rcv_handler(uint32_t events, struct epoll_event_handler *h)
{
  struct sockaddr_llc from;
  socklen_t fromlen = sizeof(from);
  int cc;
  unsigned char buf[2048];

  cc = recvfrom(h->fd, &buf, sizeof(buf), 0,
                (struct sockaddr *) &from, &fromlen);
  if (cc <= 0) {
    ERROR("recvfrom failed: %m");
    return;
  }

#if 0  
  printf("Src %02x:%02x:%02x:%02x:%02x:%02x\n",
         from.sllc_mac[0], from.sllc_mac[1],
         from.sllc_mac[2], from.sllc_mac[3],
         from.sllc_mac[4], from.sllc_mac[5]);
  int i, j;
  for (i = 0; i < cc; i += 16) {
    for (j = 0; j < 16 && i+j < cc; j++)
      printf(" %02x", buf[i+j]);
    printf("\n");
  }
  printf("\n");
  fflush(stdout);
#endif

  bpdu_rcv(h->arg, buf, cc);
}


/* We added name as an arg here because we can't do if_indextoname here,
   That needs <net/if.h> which conflicts with <linux/if.h> */
/* Needs fixing. Socket should be closed in case of errors */
int bpdu_sock_create(struct epoll_event_handler *h,
                     int if_index, char *name, struct ifdata *arg)
{
  struct sockaddr_llc llc_addr;
  memset(&llc_addr, 0, sizeof(llc_addr));
  llc_addr.sllc_family = AF_LLC;
  llc_addr.sllc_arphrd = ARPHRD_ETHER;
  llc_addr.sllc_sap = LLC_SAP_BSPAN;

  int s;
  TSTM((s = socket(AF_LLC, SOCK_DGRAM, 0)) >= 0, -1, "%m");
  
  TST(get_hwaddr(name, llc_addr.sllc_mac) == 0, -1);
  
  TSTM(bind(s, (struct sockaddr *) &llc_addr, sizeof(llc_addr)) == 0, -1,
       "Can't bind to LLC SAP %#x: %m", llc_addr.sllc_sap);
  {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    ifr.ifr_hwaddr.sa_family = AF_UNSPEC;
    memcpy(ifr.ifr_hwaddr.sa_data, stp_mc, ETH_ALEN);

    TSTM(ioctl(s, SIOCADDMULTI, &ifr) == 0, -1,
         "can't set multicast address for %s: %m", ifr.ifr_name);
  }

  TSTM(fcntl(s, F_SETFL, O_NONBLOCK) == 0, -1, "%m");
  
  h->fd = s;
  h->arg = arg;
  h->handler = bpdu_rcv_handler;

  if (add_epoll(h) < 0)
    return -1;

  return 0;
}

void bpdu_sock_delete(struct epoll_event_handler *h)
{
  remove_epoll(h);
  close(h->fd);
}


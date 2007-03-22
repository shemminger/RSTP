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
	   Stephen Hemminger <shemminger@linux-foundation.org>

******************************************************************************/

#include "packet.h"
#include "epoll_loop.h"
#include "netif_utils.h"
#include "bridge_ctl.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <netinet/in.h>

#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/filter.h>

#include "log.h"

#define DEBUG 1

/*
 * To send/receive Spanning Tree packets we use PF_PACKET because
 * it allows the filtering we want but gives raw data
 */
void packet_send(struct epoll_event_handler *h, unsigned char *data, int len)
{
	int l;

	if (fcntl(h->fd, F_SETFL, 0) < 0)
		ERROR("Error unsetting O_NONBLOCK: %m");

	l = send(h->fd, data, len, 0);
	if (l < 0)
		ERROR("send failed: %m");
	else if (l != len)
		ERROR("short write in sendto: %d instead of %d", l, len);

	if (fcntl(h->fd, F_SETFL, O_NONBLOCK) < 0)
		ERROR("Error setting O_NONBLOCK: %m");
}

void packet_rcv_handler(uint32_t events, struct epoll_event_handler *h)
{
	int cc;
	unsigned char buf[2048];

	cc = recv(h->fd, &buf, sizeof(buf), 0);
	if (cc <= 0) {
		ERROR("read failed: %m");
		return;
	}

#ifdef DEBUG
	printf("Src %02x:%02x:%02x:%02x:%02x:%02x\n",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

	int i, j;
	for (i = 0; i < cc; i += 16) {
		for (j = 0; j < 16 && i + j < cc; j++)
			printf(" %02x", buf[i + j]);
		printf("\n");
	}
	printf("\n");
	fflush(stdout);
#endif

	bridge_bpdu_rcv(h->arg, buf, cc);
}

/* Berkeley Packet filter code to filter out spanning tree packets.
   from tcpdump -dd stp
 */
static struct sock_filter stp_filter[] = {
	{ 0x28, 0, 0, 0x0000000c },
	{ 0x25, 3, 0, 0x000005dc },
	{ 0x30, 0, 0, 0x0000000e },
	{ 0x15, 0, 1, 0x00000042 },
	{ 0x6, 0, 0, 0x00000060 },
	{ 0x6, 0, 0, 0x00000000 },
};

/*
 * Open up a raw packet socket to catch all 802.2 packets on a device
 * and install a packet filter to only see STP (SAP 42)
 */
int packet_sock_create(struct epoll_event_handler *h, int if_index,
		       struct ifdata *arg)
{
	int s;
	struct sockaddr_ll sll = { 
		.sll_family = AF_PACKET,
		.sll_ifindex = if_index,
	};
	struct sock_fprog prog = {
		.len = sizeof(stp_filter) / sizeof(stp_filter[0]),
		.filter = stp_filter,
	};

	s = socket(PF_PACKET, SOCK_PACKET, htons(ETH_P_802_2));
	if (s < 0) {
		ERROR("socket failed: %m");
		return -1;
	}

	if (bind(s, (struct sockaddr *) &sll, sizeof(sll)) < 0)
		ERROR("bind failed: %m");
	
	else if (setsockopt(s, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog)) < 0) 
		ERROR("setsockopt packet filter failed: %m");

	else if (fcntl(s, F_SETFL, O_NONBLOCK) < 0)
		ERROR("fcntl set nonblock failed: %m");

	else {
		h->fd = s;
		h->arg = arg;
		h->handler = packet_rcv_handler;

		if (add_epoll(h) == 0)
			return 0;
	}

	close(s);
	return -1;
}

void packet_sock_delete(struct epoll_event_handler *h)
{
	remove_epoll(h);
	close(h->fd);
}

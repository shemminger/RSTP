/*
 * brstate.c	RTnetlink port state change
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *
 *              Modified by Srinivas Aji <Aji_Srinivas@emc.com> for use
 *              in RSTP daemon. - 2006-09-01
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_bridge.h>
#include <string.h>

#include "libnetlink.h"

#if 0
static const char *port_states[] = {
	[BR_STATE_DISABLED] = "disabled",
	[BR_STATE_LISTENING] = "listening",
	[BR_STATE_LEARNING] = "learning",
	[BR_STATE_FORWARDING] = "forwarding",
	[BR_STATE_BLOCKING] = "blocking",
};

static int portstate(const char *name)
{
	int i;

	for (i = 0; i < sizeof(port_states)/sizeof(port_states[0]); i++) {
		if (strcasecmp(name,  port_states[i]) == 0)
			return i;
	}
	return -1;
}
#endif

static int br_set_state(struct rtnl_handle *rth, unsigned ifindex, __u8 state)
{
	struct {
		struct nlmsghdr 	n;
		struct ifinfomsg 	ifi;
		char   			buf[256];
	} req;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST|NLM_F_REPLACE;
	req.n.nlmsg_type = RTM_SETLINK;
	req.ifi.ifi_family = AF_BRIDGE;
	req.ifi.ifi_index = ifindex;

	addattr32(&req.n, sizeof(req.buf), IFLA_PROTINFO, state);
	
	return rtnl_talk(rth, &req.n, 0, 0, NULL, NULL, NULL);
}

static int br_send_bpdu(struct rtnl_handle *rth, unsigned ifindex,
                        const unsigned char *data, int len)
{
	struct {
		struct nlmsghdr 	n;
		struct ifinfomsg 	ifi;
		char   			buf[256];
	} req;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST|NLM_F_REPLACE;
	req.n.nlmsg_type = RTM_SETLINK;
	req.ifi.ifi_family = AF_BRIDGE;
	req.ifi.ifi_index = ifindex;

	addattr_l(&req.n, sizeof(req.buf), IFLA_PRIORITY, data, len);
	
	return rtnl_talk(rth, &req.n, 0, 0, NULL, NULL, NULL);
}

#if 0
int main(int argc, char **argv)
{
	unsigned int ifindex;
	int err, brstate;
	struct rtnl_handle rth;


	if (argc != 3) {
		fprintf(stderr,
			"Usage: brstate ifname state\n");
		exit(-1);
	}

	if (rtnl_open(&rth, 0) < 0) {
		fprintf(stderr, "brstate: can't setup netlink\n");
		exit(1);
	}

	ifindex = if_nametoindex(argv[1]);
	if (ifindex == 0) {
		fprintf(stderr, "brstate: unknown interface '%s'\n", argv[1]);
		exit(1);
	}
	
	brstate = portstate(argv[2]);
	if (brstate < 0) {
		fprintf(stderr, "brstate: unknown port state '%s'\n",
			argv[2]);
		exit(1);
	}

	err = br_set_state(&rth, ifindex, brstate);
	if (err) {
		fprintf(stderr, "brstate: set  %d, %d failed %d\n",
			 ifindex, brstate, err);
		exit(1);
	}

	rtnl_close(&rth);
	return 0;
}
#endif

#include "bridge_ctl.h"

extern struct rtnl_handle rth_state;

int bridge_set_state(int ifindex, int brstate)
{
  int err = br_set_state(&rth_state, ifindex, brstate);
  if (err < 0) {
    fprintf(stderr, "Couldn't set bridge state, ifindex %d, state %d\n",
            ifindex, brstate);
    return -1;
  }
  return 0;
}

int bridge_send_bpdu(int ifindex, const unsigned char *data, int len)
{
  int err = br_send_bpdu(&rth_state, ifindex, data, len);
  if (err < 0) {
    fprintf(stderr, "Couldn't send bpdu, ifindex %d\n", ifindex);
    return -1;
  }
  return 0;
}

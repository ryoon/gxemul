/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: net.c,v 1.2 2004-07-05 19:25:04 debug Exp $
 *
 *  Emulated (ethernet) network support.
 *
 *  The emulated NIC has a MAC address of (for example) 11:22:33:44:55:66.
 *  From the emulated environment, the only other machine existing on the
 *  network is a "gateway" or "firewall", which has an address of
 *  55:44:33:22:11:00.  This module (net.c) contains the emulation of that
 *  gateway.
 *
 *  NOTE: The 'extra' argument used in many functions in this file is a pointer
 *  to something unique for each controller, so that if multiple controllers
 *  are emulated concurrently, they will not get packets that aren't meant
 *  for some other controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "misc.h"
#include "net.h"


#define debug fatal


struct ethernet_packet_link {
	struct ethernet_packet_link *prev;
	struct ethernet_packet_link *next;

	void		*extra;
	unsigned char	*data;
	int		len;
};

static struct ethernet_packet_link *first_ethernet_packet = NULL;
static struct ethernet_packet_link *last_ethernet_packet = NULL;

unsigned char gateway_addr[6] = { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
unsigned char gateway_ipv4[4] = { 10, 0, 0, 1 };


/*
 *  net_arp():
 *
 *  Handle an ARP packet, coming from the emulated NIC.
 *
 *  An ARP packet might look like this:
 *
 *	ARP header:
 *	    ARP hardware addr family:	0001
 *	    ARP protocol addr family:	0800
 *	    ARP addr lengths:		06 04
 *	    ARP request:		0001
 *	    ARP from:			112233445566 01020304
 *	    ARP to:			000000000000 01020301
 *	Fill:				000000000000000000000000000000000000
 *
 *  An ARP request with a 'to' IP value of the gateway should cause an
 *  ARP response packet to be created.
 *
 *  An ARP request with the same from and to IP addresses should be ignored.
 *  (This would be a host testing to see if there is an IP collision.)
 */
static void net_arp(void *extra, unsigned char *packet, int len)
{
	int i;

	debug("[ net: ARP: ");
	for (i=0; i<2; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=2; i<4; i++)
		debug("%02x", packet[i]);
	debug(" ");
	debug("%02x", packet[4]);
	debug(" ");
	debug("%02x", packet[5]);
	debug(" ");
	debug("%02x", packet[6]);	/*  Request type  */
	debug("%02x", packet[7]);
	debug(" ");
	for (i=8; i<18; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=18; i<28; i++)
		debug("%02x", packet[i]);
	debug(" ]\n");
}


/*
 *  net_ethernet_rx_avail():
 *
 *  Return 1 if there is a packet available for this 'extra' pointer, otherwise
 *  return 0.
 *
 *  (This function is basically net_ethernet_rx() but it only receives a return
 *  value telling us whether there is a packet or not, we don't actually get
 *  the packet.)
 */
int net_ethernet_rx_avail(void *extra)
{
	return net_ethernet_rx(extra, NULL, NULL);
}


/*
 *  net_ethernet_rx():
 *
 *  Receive an ethernet packet. (This means handing over an already prepared
 *  packet from this module (net.c) to a specific ethernet controller device.)
 *
 *  Return value is 1 if there was a packet available. *packetp and *lenp
 *  will be set to the packet's data pointer and length, respectively, and
 *  the packet will be removed from the linked list). If there was no packet
 *  available, 0 is returned.
 *
 *  If packetp is NULL, then the search is aborted as soon as a packet with
 *  the correct 'extra' field is found, and a 1 is returned, but as packetp
 *  is NULL we can't return the actual packet. (This is the internal form
 *  if net_ethernet_rx_avail().)
 */
int net_ethernet_rx(void *extra, unsigned char **packetp, int *lenp)
{
	struct ethernet_packet_link *lp, *prev;

	/*  Find the first packet which has the right 'extra' field.  */

	lp = first_ethernet_packet;
	prev = NULL;
	while (lp != NULL) {
		if (lp->extra == extra) {
			/*  We found a packet for this controller!  */
			if (packetp == NULL || lenp == NULL)
				return 1;

			/*  Let's return it:  */
			(*packetp) = lp->data;
			(*lenp) = lp->len;

			/*  Remove this link from the linked list:  */
			if (prev == NULL)
				first_ethernet_packet = lp->next;
			else
				prev->next = lp->next;

			if (lp->next == NULL)
				last_ethernet_packet = prev;
			else
				lp->next->prev = prev;

			free(lp);

			/*  ... and return successfully:  */
			return 1;
		}

		prev = lp;
		lp = lp->next;
	}

	/*  No packet found. :-(  */
	return 0;
}


/*
 *  net_ethernet_tx():
 *
 *  Transmit an ethernet packet, as seen from the emulated ethernet controller.
 *  If the packet can be handled here, it will not neccessarily be transmitted
 *  to the outside world.
 */
void net_ethernet_tx(void *extra, unsigned char *packet, int len)
{
	int i;

	debug("[ net: ethernet: ");
	for (i=0; i<6; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=6; i<12; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=12; i<14; i++)
		debug("%02x", packet[i]);
	debug(" ");
	for (i=14; i<len; i++)
		debug("%02x", packet[i]);
	debug(" ]\n");

	/*  ARP:  */
	if (len == 60 && packet[12] == 0x08 && packet[13] == 0x06) {
		net_arp(extra, packet + 14, len - 14);
		return;
	}

	/*  IP:  */
/*
	if (packet[12] == 0x08 && packet[13] == 0x00) {
		net_ip(extra, packet, len);
	}
*/

	fatal("[ net: TX: UNIMPLEMENTED ethernet packet type 0x%02%02! ]\n",
	    packet[12], packet[13]);
}


/*
 *  net_init():
 *
 *  This function should be called before any other net_*() functions are
 *  used.
 */
void net_init(void)
{
	first_ethernet_packet = last_ethernet_packet = NULL;
}


/*
 *  Copyright (C) 2004-2006  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 *  $Id: net.c,v 1.2 2006-09-02 06:21:55 debug Exp $
 *
 *  Emulated (ethernet / internet) network support.
 *
 *
 *  NOTE:  This is just an ugly hack, and just barely enough to get some
 *         Internet networking up and running for the guest OS.
 *
 *  TODO:
 *	o)  TCP: fin/ack stuff, and connection time-outs and
 *	    connection refused (reset on connect?), resend
 *	    data to the guest OS if no ack has arrived for
 *	    some time (? buffers?)
 *		http://www.tcpipguide.com/free/t_TCPConnectionTermination-2.htm
 *	o)  remove the netbsd-specific options in the tcp header (?)
 *	o)  Outgoing UDP packet fragment support.
 *	o)  IPv6  (outgoing, incoming, and the nameserver/gateway)
 *	o)  Incoming connections
 *
 *  TODO 2: The following comments are old! Fix this.
 *
 *
 *  The emulated NIC has a MAC address of (for example) 10:20:30:00:00:10.
 *  From the emulated environment, the only other machine existing on the
 *  network is a "gateway" or "firewall", which has an address of
 *  60:50:40:30:20:10. This module (net.c) contains the emulation of that
 *  gateway. It works like a NAT firewall, but emulated in userland software.
 *
 *  The gateway uses IPv4 address 10.0.0.254, the guest OS (inside the
 *  emulator) could use any 10.x.x.x address, except 10.0.0.254. A suitable
 *  choice is, for example 10.0.0.1.
 *
 *
 *  NOTE: The 'extra' argument used in many functions in this file is a pointer
 *  to something unique for each controller, so that if multiple controllers
 *  are emulated concurrently, they will not get packets that aren't meant
 *  for some other controller.
 *
 *
 *	|------------------  a network  --------------------------------|
 *		^               ^				^
 *		|               |				|
 *	    a NIC connected    another NIC                the gateway
 *	    to the network					|
 *								v
 *							     outside
 *							      world
 *
 *  The gateway isn't connected as a NIC, but is an "implicit" machine on the
 *  network.
 *
 *  (See http://www.sinclair.org.au/keith/networking/vendor.html for a list
 *  of ethernet MAC assignments.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>

#include "machine.h"
#include "misc.h"
#include "net.h"


/*  #define debug fatal  */


/*
 *  net_allocate_packet_link():
 *
 *  This routine allocates an ethernet_packet_link struct, and adds it at
 *  the end of the packet chain.  A data buffer is allocated (and zeroed),
 *  and the data, extra, and len fields of the link are set.
 *
 *  Return value is a pointer to the link on success. It doesn't return on
 *  failure.
 */
struct ethernet_packet_link *net_allocate_packet_link(
	struct net *net, void *extra, int len)
{
	struct ethernet_packet_link *lp;

	lp = malloc(sizeof(struct ethernet_packet_link));
	if (lp == NULL) {
		fprintf(stderr, "net_allocate_packet_link(): out of memory\n");
		exit(1);
	}

	/*  memset(lp, 0, sizeof(struct ethernet_packet_link));  */

	lp->len = len;
	lp->extra = extra;
	lp->data = malloc(len);
	if (lp->data == NULL) {
		fprintf(stderr, "net_allocate_packet_link(): out of memory\n");
		exit(1);
	}
	lp->next = NULL;

	/*  TODO: maybe this is not necessary:  */
	memset(lp->data, 0, len);

	/*  Add last in the link chain:  */
	lp->prev = net->last_ethernet_packet;
	if (lp->prev != NULL)
		lp->prev->next = lp;
	else
		net->first_ethernet_packet = lp;
	net->last_ethernet_packet = lp;

	return lp;
}


/*
 *  net_arp():
 *
 *  Handle an ARP (or RARP) packet, coming from the emulated NIC.
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
 *
 *  An ARP request with a 'to' IP value of the gateway should cause an
 *  ARP response packet to be created.
 *
 *  An ARP request with the same from and to IP addresses should be ignored.
 *  (This would be a host testing to see if there is an IP collision.)
 */
static void net_arp(struct net *net, void *extra,
	unsigned char *packet, int len, int reverse)
{
	int q;
	int i;

	/*  TODO: This debug dump assumes ethernet->IPv4 translation:  */
	if (reverse)
		debug("[ net: RARP: ");
	else
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
	debug(" req=");
	debug("%02x", packet[6]);	/*  Request type  */
	debug("%02x", packet[7]);
	debug(" from=");
	for (i=8; i<18; i++)
		debug("%02x", packet[i]);
	debug(" to=");
	for (i=18; i<28; i++)
		debug("%02x", packet[i]);
	debug(" ]\n");

	if (packet[0] == 0x00 && packet[1] == 0x01 &&
	    packet[2] == 0x08 && packet[3] == 0x00 &&
	    packet[4] == 0x06 && packet[5] == 0x04) {
		int r = (packet[6] << 8) + packet[7];
		struct ethernet_packet_link *lp;

		switch (r) {
		case 1:		/*  Request  */
			/*  Only create a reply if this was meant for the
			    gateway:  */
			if (memcmp(packet+24, net->gateway_ipv4_addr, 4) != 0)
				break;

			lp = net_allocate_packet_link(net, extra, 60 + 14);

			/*  Copy the old packet first:  */
			memset(lp->data, 0, 60 + 14);
			memcpy(lp->data + 14, packet, len);

			/*  Add ethernet ARP header:  */
			memcpy(lp->data + 0, lp->data + 8 + 14, 6);
			memcpy(lp->data + 6, net->gateway_ethernet_addr, 6);
			lp->data[12] = 0x08; lp->data[13] = 0x06;

			/*  Address of the emulated machine:  */
			memcpy(lp->data + 18 + 14, lp->data + 8 + 14, 10);

			/*  Address of the gateway:  */
			memcpy(lp->data +  8 + 14, net->gateway_ethernet_addr,
			    6);
			memcpy(lp->data + 14 + 14, net->gateway_ipv4_addr, 4);

			/*  This is a Reply:  */
			lp->data[6 + 14] = 0x00; lp->data[7 + 14] = 0x02;

			break;
		case 3:		/*  Reverse Request  */
			lp = net_allocate_packet_link(net, extra, 60 + 14);

			/*  Copy the old packet first:  */
			memset(lp->data, 0, 60 + 14);
			memcpy(lp->data + 14, packet, len);

			/*  Add ethernet RARP header:  */
			memcpy(lp->data + 0, packet + 8, 6);
			memcpy(lp->data + 6, net->gateway_ethernet_addr, 6);
			lp->data[12] = 0x80; lp->data[13] = 0x35;

			/*  This is a RARP reply:  */
			lp->data[6 + 14] = 0x00; lp->data[7 + 14] = 0x04;

			/*  Address of the gateway:  */
			memcpy(lp->data +  8 + 14, net->gateway_ethernet_addr,
			    6);
			memcpy(lp->data + 14 + 14, net->gateway_ipv4_addr, 4);

			/*  MAC address of emulated machine:  */
			memcpy(lp->data + 18 + 14, packet + 8, 6);

			/*
			 *  IP address of the emulated machine:  Automagically
			 *  generated from the MAC address. :-)
			 *
			 *  packet+8 points to the client's mac address,
			 *  for example 10:20:30:00:00:z0, where z is 0..15.
			 *  10:20:30:00:00:10 results in 10.0.0.1.
			 */
			/*  q = (packet[8 + 3]) >> 4;  */
			/*  q = q*15 + ((packet[8 + 4]) >> 4);  */
			q = (packet[8 + 5]) >> 4;
			lp->data[24 + 14] = 10;
			lp->data[25 + 14] =  0;
			lp->data[26 + 14] =  0;
			lp->data[27 + 14] =  q;
			break;
		case 2:		/*  Reply  */
		case 4:		/*  Reverse Reply  */
		default:
			fatal("[ net: ARP: UNIMPLEMENTED request type "
			    "0x%04x ]\n", r);
		}
	} else {
		fatal("[ net: ARP: UNIMPLEMENTED arp packet type: ");
		for (i=0; i<len; i++)
			fatal("%02x", packet[i]);
		fatal(" ]\n");
	}
}


/*
 *  net_ethernet_rx_avail():
 *
 *  Return 1 if there is a packet available for this 'extra' pointer, otherwise
 *  return 0.
 *
 *  Appart from actually checking for incoming packets from the outside world,
 *  this function basically works like net_ethernet_rx() but it only receives
 *  a return value telling us whether there is a packet or not, we don't
 *  actually get the packet.
 */
int net_ethernet_rx_avail(struct net *net, void *extra)
{
	int received_packets_this_tick = 0;
	int max_packets_this_tick = 200;
	int con_id;

	if (net == NULL)
		return 0;

	/*
	 *  If the network is distributed across multiple emulator processes,
	 *  then receive incoming packets from those processes.
	 */
	if (net->local_port != 0) {
		struct sockaddr_in si;
		socklen_t si_len = sizeof(si);
		int res, i;
		unsigned char buf[60000];

		if ((res = recvfrom(net->local_port_socket, buf, sizeof(buf), 0,
		    (struct sockaddr *)&si, &si_len)) != -1) {
			/*  fatal("DISTRIBUTED packet, %i bytes from %s:%d\n",
			    res, inet_ntoa(si.sin_addr), ntohs(si.sin_port)); */
			for (i=0; i<net->n_nics; i++) {
				struct ethernet_packet_link *lp;
				lp = net_allocate_packet_link(net,
				    net->nic_extra[i], res);
				memcpy(lp->data, buf, res);
			}
		}
	}

	/*
	 *  UDP:
	 */
	for (con_id=0; con_id<MAX_UDP_CONNECTIONS; con_id++) {
		ssize_t res;
		unsigned char buf[66000];
		unsigned char udp_data[66008];
		struct sockaddr_in from;
		socklen_t from_len = sizeof(from);
		int ip_len, udp_len;
		struct ethernet_packet_link *lp;
		int max_per_packet;
		int bytes_converted = 0;
		int this_packets_data_length;
		int fragment_ofs = 0;

		if (received_packets_this_tick > max_packets_this_tick)
			break;

		if (!net->udp_connections[con_id].in_use)
			continue;

		if (net->udp_connections[con_id].socket < 0) {
			fatal("INTERNAL ERROR in net.c, udp socket < 0 "
			    "but in use?\n");
			continue;
		}

		res = recvfrom(net->udp_connections[con_id].socket, buf,
		    sizeof(buf), 0, (struct sockaddr *)&from, &from_len);

		/*  No more incoming UDP on this connection?  */
		if (res < 0)
			continue;

		net->timestamp ++;
		net->udp_connections[con_id].last_used_timestamp =
		    net->timestamp;

		net->udp_connections[con_id].udp_id ++;

		/*
		 *  Special case for the nameserver:  If a UDP packet is
		 *  received from the nameserver (if the nameserver's IP is
		 *  known), fake it so that it comes from the gateway instead.
		 */
		if (net->udp_connections[con_id].fake_ns)
			memcpy(((unsigned char *)(&from))+4,
			    &net->gateway_ipv4_addr[0], 4);

		/*
		 *  We now have a UDP packet of size 'res' which we need
		 *  turn into one or more ethernet packets for the emulated
		 *  operating system.  Ethernet packets are at most 1518
		 *  bytes long. With some margin, that means we can have
		 *  about 1500 bytes per packet.
		 *
		 *	Ethernet = 14 bytes
		 *	IP = 20 bytes
		 *	(UDP = 8 bytes + data)
		 *
		 *  So data can be at most max_per_packet - 34. For UDP
		 *  fragments, each multiple should (?) be a multiple of
		 *  8 bytes, except the last which doesn't have any such
		 *  restriction.
		 */
		max_per_packet = 1500;

		/*  UDP:  */
		udp_len = res + 8;
		/*  from[2..3] = outside_udp_port  */
		udp_data[0] = ((unsigned char *)&from)[2];
		udp_data[1] = ((unsigned char *)&from)[3];
		udp_data[2] = (net->udp_connections[con_id].
		    inside_udp_port >> 8) & 0xff;
		udp_data[3] = net->udp_connections[con_id].
		    inside_udp_port & 0xff;
		udp_data[4] = udp_len >> 8;
		udp_data[5] = udp_len & 0xff;
		udp_data[6] = 0;
		udp_data[7] = 0;
		memcpy(udp_data + 8, buf, res);
		/*
		 *  TODO:  UDP checksum, if necessary. At least NetBSD
		 *  and OpenBSD accept UDP packets with 0x0000 in the
		 *  checksum field anyway.
		 */

		while (bytes_converted < udp_len) {
			this_packets_data_length = udp_len - bytes_converted;

			/*  Do we need to fragment?  */
			if (this_packets_data_length > max_per_packet-34) {
				this_packets_data_length =
				    max_per_packet - 34;
				while (this_packets_data_length & 7)
					this_packets_data_length --;
			}

			ip_len = 20 + this_packets_data_length;

			lp = net_allocate_packet_link(net, extra,
			    14 + 20 + this_packets_data_length);

			/*  Ethernet header:  */
			memcpy(lp->data + 0, net->udp_connections[con_id].
			    ethernet_address, 6);
			memcpy(lp->data + 6, net->gateway_ethernet_addr, 6);
			lp->data[12] = 0x08;	/*  IP = 0x0800  */
			lp->data[13] = 0x00;

			/*  IP header:  */
			lp->data[14] = 0x45;	/*  ver  */
			lp->data[15] = 0x00;	/*  tos  */
			lp->data[16] = ip_len >> 8;
			lp->data[17] = ip_len & 0xff;
			lp->data[18] = net->udp_connections[con_id].udp_id >> 8;
			lp->data[19] = net->udp_connections[con_id].udp_id
			    & 0xff;
			lp->data[20] = (fragment_ofs >> 8);
			if (bytes_converted + this_packets_data_length
			    < udp_len)
				lp->data[20] |= 0x20;	/*  More fragments  */
			lp->data[21] = fragment_ofs & 0xff;
			lp->data[22] = 0x40;	/*  ttl  */
			lp->data[23] = 17;	/*  p = UDP  */
			lp->data[26] = ((unsigned char *)&from)[4];
			lp->data[27] = ((unsigned char *)&from)[5];
			lp->data[28] = ((unsigned char *)&from)[6];
			lp->data[29] = ((unsigned char *)&from)[7];
			memcpy(lp->data + 30, net->udp_connections[con_id].
			    inside_ip_address, 4);
			net_ip_checksum(lp->data + 14, 10, 20);

			memcpy(lp->data+34, udp_data + bytes_converted,
			    this_packets_data_length);

			bytes_converted += this_packets_data_length;
			fragment_ofs = bytes_converted / 8;

			received_packets_this_tick ++;
		}

		/*  This makes sure we check this connection AGAIN
		    for more incoming UDP packets, before moving to the
		    next connection:  */
		con_id --;
	}

	/*
	 *  TCP:
	 */
	for (con_id=0; con_id<MAX_TCP_CONNECTIONS; con_id++) {
		unsigned char buf[66000];
		ssize_t res, res2;
		fd_set rfds;
		struct timeval tv;

		if (received_packets_this_tick > max_packets_this_tick)
			break;

		if (!net->tcp_connections[con_id].in_use)
			continue;

		if (net->tcp_connections[con_id].socket < 0) {
			fatal("INTERNAL ERROR in net.c, tcp socket < 0"
			    " but in use?\n");
			continue;
		}

		if (net->tcp_connections[con_id].incoming_buf == NULL) {
			net->tcp_connections[con_id].incoming_buf =
			    malloc(TCP_INCOMING_BUF_LEN);
			if (net->tcp_connections[con_id].incoming_buf == NULL) {
				printf("out of memory allocating "
				    "incoming_buf for con_id %i\n", con_id);
				exit(1);
			}
		}

		if (net->tcp_connections[con_id].state >=
		    TCP_OUTSIDE_DISCONNECTED)
			continue;

		/*  Is the socket available for output?  */
		FD_ZERO(&rfds);		/*  write  */
		FD_SET(net->tcp_connections[con_id].socket, &rfds);
		tv.tv_sec = tv.tv_usec = 0;
		errno = 0;
		res = select(net->tcp_connections[con_id].socket+1,
		    NULL, &rfds, NULL, &tv);

		if (errno == ECONNREFUSED) {
			fatal("[ ECONNREFUSED: TODO ]\n");
			net->tcp_connections[con_id].state =
			    TCP_OUTSIDE_DISCONNECTED;
			fatal("CHANGING TO TCP_OUTSIDE_DISCONNECTED "
			    "(refused connection)\n");
			continue;
		}

		if (errno == ETIMEDOUT) {
			fatal("[ ETIMEDOUT: TODO ]\n");
			/*  TODO  */
			net->tcp_connections[con_id].state =
			    TCP_OUTSIDE_DISCONNECTED;
			fatal("CHANGING TO TCP_OUTSIDE_DISCONNECTED "
			    "(timeout)\n");
			continue;
		}

		if (net->tcp_connections[con_id].state ==
		    TCP_OUTSIDE_TRYINGTOCONNECT && res > 0) {
			net->tcp_connections[con_id].state =
			    TCP_OUTSIDE_CONNECTED;
			debug("CHANGING TO TCP_OUTSIDE_CONNECTED\n");
			net_ip_tcp_connectionreply(net, extra, con_id, 1,
			    NULL, 0, 0);
		}

		if (net->tcp_connections[con_id].state ==
		    TCP_OUTSIDE_CONNECTED && res < 1) {
			continue;
		}

		/*
		 *  Does this connection have unacknowledged data?  Then, if
		 *  enough number of rounds have passed, try to resend it using
		 *  the old value of seqnr.
		 */
		if (net->tcp_connections[con_id].incoming_buf_len != 0) {
			net->tcp_connections[con_id].incoming_buf_rounds ++;
			if (net->tcp_connections[con_id].incoming_buf_rounds >
			    10000) {
				debug("  at seqnr %u but backing back to %u,"
				    " resending %i bytes\n",
				    net->tcp_connections[con_id].outside_seqnr,
				    net->tcp_connections[con_id].
				    incoming_buf_seqnr,
				    net->tcp_connections[con_id].
				    incoming_buf_len);

				net->tcp_connections[con_id].
				    incoming_buf_rounds = 0;
				net->tcp_connections[con_id].outside_seqnr =
				    net->tcp_connections[con_id].
				    incoming_buf_seqnr;

				net_ip_tcp_connectionreply(net, extra, con_id,
				    0, net->tcp_connections[con_id].
				    incoming_buf,
				    net->tcp_connections[con_id].
				    incoming_buf_len, 0);
			}
			continue;
		}

		/*  Don't receive unless the guest OS is ready!  */
		if (((int32_t)net->tcp_connections[con_id].outside_seqnr -
		    (int32_t)net->tcp_connections[con_id].inside_acknr) > 0) {
/*			fatal("YOYO 1! outside_seqnr - inside_acknr = %i\n",
			    net->tcp_connections[con_id].outside_seqnr -
			    net->tcp_connections[con_id].inside_acknr);  */
			continue;
		}

		/*  Is there incoming data available on the socket?  */
		FD_ZERO(&rfds);		/*  read  */
		FD_SET(net->tcp_connections[con_id].socket, &rfds);
		tv.tv_sec = tv.tv_usec = 0;
		res2 = select(net->tcp_connections[con_id].socket+1, &rfds,
		    NULL, NULL, &tv);

		/*  No more incoming TCP data on this connection?  */
		if (res2 < 1)
			continue;

		res = read(net->tcp_connections[con_id].socket, buf, 1400);
		if (res > 0) {
			/*  debug("\n -{- %lli -}-\n", (long long)res);  */
			net->tcp_connections[con_id].incoming_buf_len = res;
			net->tcp_connections[con_id].incoming_buf_rounds = 0;
			net->tcp_connections[con_id].incoming_buf_seqnr = 
			    net->tcp_connections[con_id].outside_seqnr;
			debug("  putting %i bytes (seqnr %u) in the incoming "
			    "buf\n", res, net->tcp_connections[con_id].
			    incoming_buf_seqnr);
			memcpy(net->tcp_connections[con_id].incoming_buf,
			    buf, res);

			net_ip_tcp_connectionreply(net, extra, con_id, 0,
			    buf, res, 0);
		} else if (res == 0) {
			net->tcp_connections[con_id].state =
			    TCP_OUTSIDE_DISCONNECTED;
			debug("CHANGING TO TCP_OUTSIDE_DISCONNECTED, read"
			    " res=0\n");
			net_ip_tcp_connectionreply(net, extra, con_id, 0,
			    NULL, 0, 0);
		} else {
			net->tcp_connections[con_id].state =
			    TCP_OUTSIDE_DISCONNECTED;
			fatal("CHANGING TO TCP_OUTSIDE_DISCONNECTED, "
			    "read res<=0, errno = %i\n", errno);
			net_ip_tcp_connectionreply(net, extra, con_id, 0,
			    NULL, 0, 0);
		}

		net->timestamp ++;
		net->tcp_connections[con_id].last_used_timestamp =
		    net->timestamp;
	}

	return net_ethernet_rx(net, extra, NULL, NULL);
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
int net_ethernet_rx(struct net *net, void *extra,
	unsigned char **packetp, int *lenp)
{
	struct ethernet_packet_link *lp, *prev;

	if (net == NULL)
		return 0;

	/*  Find the first packet which has the right 'extra' field.  */

	lp = net->first_ethernet_packet;
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
				net->first_ethernet_packet = lp->next;
			else
				prev->next = lp->next;

			if (lp->next == NULL)
				net->last_ethernet_packet = prev;
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
 *  send_udp():
 *
 *  Send a simple UDP packet to some other (real) host. Used for distributed
 *  network simulations.
 */
void send_udp(struct in_addr *addrp, int portnr, unsigned char *packet,
	size_t len)
{
	int s;
	struct sockaddr_in si;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("send_udp(): socket");
		return;
	}

	/*  fatal("send_udp(): sending to port %i\n", portnr);  */

	si.sin_family = AF_INET;
	si.sin_addr = *addrp;
	si.sin_port = htons(portnr);

	if (sendto(s, packet, len, 0, (struct sockaddr *)&si,
	    sizeof(si)) != (ssize_t)len) {
		perror("send_udp(): sendto");
	}

	close(s);
}


/*
 *  net_ethernet_tx():
 *
 *  Transmit an ethernet packet, as seen from the emulated ethernet controller.
 *  If the packet can be handled here, it will not necessarily be transmitted
 *  to the outside world.
 */
void net_ethernet_tx(struct net *net, void *extra,
	unsigned char *packet, int len)
{
	int i, n;

	if (net == NULL)
		return;

	/*  Drop too small packets:  */
	if (len < 20)
		return;

	/*
	 *  Copy this packet to all other NICs on this network:
	 */
	if (extra != NULL && net->n_nics > 0) {
		for (i=0; i<net->n_nics; i++)
			if (extra != net->nic_extra[i]) {
				struct ethernet_packet_link *lp;
				lp = net_allocate_packet_link(net,
				    net->nic_extra[i], len);

				/*  Copy the entire packet:  */
				memcpy(lp->data, packet, len);
			}
	}

	/*
	 *  If this network is distributed across multiple emulator processes,
	 *  then transmit the packet to those other processes.
	 */
	if (net->remote_nets != NULL) {
		struct remote_net *rnp = net->remote_nets;
		while (rnp != NULL) {
			send_udp(&rnp->ipv4_addr, rnp->portnr, packet, len);
			rnp = rnp->next;
		}
	}

	/*  Drop packets that are not destined for the gateway:  */
	if (memcmp(packet, net->gateway_ethernet_addr, 6) != 0
	    && packet[0] != 0xff && packet[0] != 0x00)
		return;

	/*
	 *  The code below simulates the behaviour of a "NAT"-style
	 *  gateway.
	 */
#if 0
	fatal("[ net: ethernet: ");
	for (i=0; i<6; i++)	fatal("%02x", packet[i]); fatal(" ");
	for (i=6; i<12; i++)	fatal("%02x", packet[i]); fatal(" ");
	for (i=12; i<14; i++)	fatal("%02x", packet[i]); fatal(" ");
	for (i=14; i<len; i++)	fatal("%02x", packet[i]); fatal(" ]\n");
#endif

	/*  Sprite:  */
	if (packet[12] == 0x05 && packet[13] == 0x00) {
		/*  TODO.  */
		fatal("[ net: TX: UNIMPLEMENTED Sprite packet ]\n");
		return;
	}

	/*  IP:  */
	if (packet[12] == 0x08 && packet[13] == 0x00) {
		/*  Routed via the gateway?  */
		if (memcmp(packet+0, net->gateway_ethernet_addr, 6) == 0) {
			net_ip(net, extra, packet, len);
			return;
		}

		/*  Broadcast? (DHCP does this.)  */
		n = 0;
		for (i=0; i<6; i++)
			if (packet[i] == 0xff)
				n++;
		if (n == 6) {
			net_ip_broadcast(net, extra, packet, len);
			return;
		}

		if (net->n_nics < 2) {
			fatal("[ net: TX: IP packet not for gateway, "
			    "and not broadcast: ");
			for (i=0; i<14; i++)
				fatal("%02x", packet[i]);
			fatal(" ]\n");
		}
		return;
	}

	/*  ARP:  */
	if (packet[12] == 0x08 && packet[13] == 0x06) {
		if (len != 42 && len != 60)
			fatal("[ net_ethernet_tx: WARNING! unusual "
			    "ARP len (%i) ]\n", len);
		net_arp(net, extra, packet + 14, len - 14, 0);
		return;
	}

	/*  RARP:  */
	if (packet[12] == 0x80 && packet[13] == 0x35) {
		net_arp(net, extra, packet + 14, len - 14, 1);
		return;
	}

	/*  IPv6:  */
	if (packet[12] == 0x86 && packet[13] == 0xdd) {
		/*  TODO.  */
		fatal("[ net: TX: UNIMPLEMENTED IPv6 packet ]\n");
		return;
	}

	fatal("[ net: TX: UNIMPLEMENTED ethernet packet type 0x%02x%02x! ]\n",
	    packet[12], packet[13]);
}


/*
 *  parse_resolvconf():
 *
 *  This function parses "/etc/resolv.conf" to figure out the nameserver
 *  and domain used by the host.
 */
static void parse_resolvconf(struct net *net)
{
	FILE *f;
	char buf[8000];
	size_t len;
	int res;
	unsigned int i, start;

	/*
	 *  This is a very ugly hack, which tries to figure out which
	 *  nameserver the host uses by looking for the string 'nameserver'
	 *  in /etc/resolv.conf.
	 *
	 *  This can later on be used for DHCP autoconfiguration.  (TODO)
	 *
	 *  TODO: This is hardcoded to use /etc/resolv.conf. Not all
	 *        operating systems use that filename.
	 *
	 *  TODO: This is hardcoded for AF_INET (that is, IPv4).
	 *
	 *  TODO: This assumes that the first nameserver listed is the
	 *        one to use.
	 */
	f = fopen("/etc/resolv.conf", "r");
	if (f == NULL)
		return;

	/*  TODO: get rid of the hardcoded values  */
	memset(buf, 0, sizeof(buf));
	len = fread(buf, 1, sizeof(buf) - 100, f);
	fclose(f);
	buf[sizeof(buf) - 1] = '\0';

	for (i=0; i<len; i++)
		if (strncmp(buf+i, "nameserver", 10) == 0) {
			char *p;

			/*
			 *  "nameserver" (1 or more whitespace)
			 *  "x.y.z.w" (non-digit)
			 */

			/*  debug("found nameserver at offset %i\n", i);  */
			i += 10;
			while (i<len && (buf[i]==' ' || buf[i]=='\t'))
				i++;
			if (i >= len)
				break;
			start = i;

			p = buf+start;
			while ((*p >= '0' && *p <= '9') || *p == '.')
				p++;
			*p = '\0';

#ifdef HAVE_INET_PTON
			res = inet_pton(AF_INET, buf + start,
			    &net->nameserver_ipv4);
#else
			res = inet_aton(buf + start, &net->nameserver_ipv4);
#endif
			if (res < 1)
				break;

			net->nameserver_known = 1;
			break;
		}

	for (i=0; i<len; i++)
		if (strncmp(buf+i, "domain", 6) == 0) {
			/*  "domain" (1 or more whitespace) domain_name  */
			i += 6;
			while (i<len && (buf[i]==' ' || buf[i]=='\t'))
				i++;
			if (i >= len)
				break;

			start = i;
			while (i<len && buf[i]!='\n' && buf[i]!='\r')
				i++;
			if (i < len)
				buf[i] = '\0';
			/*  fatal("DOMAIN='%s'\n", buf + start);  */
			net->domain_name = strdup(buf + start);
			break;
		}
}


/*
 *  net_add_nic():
 *
 *  Add a NIC to a network. (All NICs on a network will see each other's
 *  packets.)
 */
void net_add_nic(struct net *net, void *extra, unsigned char *macaddr)
{
	if (net == NULL)
		return;

	if (extra == NULL) {
		fprintf(stderr, "net_add_nic(): extra = NULL\n");
		exit(1);
	}

	net->n_nics ++;
	net->nic_extra = realloc(net->nic_extra, sizeof(void *)
	    * net->n_nics);
	if (net->nic_extra == NULL) {
		fprintf(stderr, "net_add_nic(): out of memory\n");
		exit(1);
	}

	net->nic_extra[net->n_nics - 1] = extra;
}


/*
 *  net_gateway_init():
 *
 *  This function creates a "gateway" machine (for example at IPv4 address
 *  10.0.0.254, if the net is 10.0.0.0/8), which acts as a gateway/router/
 *  nameserver etc.
 */
static void net_gateway_init(struct net *net)
{
	unsigned char *p = (void *) &net->netmask_ipv4;
	uint32_t x;
	int xl;

	x = (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
	xl = 32 - net->netmask_ipv4_len;
	if (xl > 8)
		xl = 8;
	x |= ((1 << xl) - 1) & ~1;

	net->gateway_ipv4_addr[0] = x >> 24;
	net->gateway_ipv4_addr[1] = x >> 16;
	net->gateway_ipv4_addr[2] = x >> 8;
	net->gateway_ipv4_addr[3] = x;

	net->gateway_ethernet_addr[0] = 0x60;
	net->gateway_ethernet_addr[1] = 0x50;
	net->gateway_ethernet_addr[2] = 0x40;
	net->gateway_ethernet_addr[3] = 0x30;
	net->gateway_ethernet_addr[4] = 0x20;
	net->gateway_ethernet_addr[5] = 0x10;
}


/*
 *  net_dumpinfo():
 *
 *  Called from the debugger's "machine" command, to print some info about
 *  a network.
 */
void net_dumpinfo(struct net *net)
{
	int iadd = DEBUG_INDENTATION;
	struct remote_net *rnp;

	debug("net: simulating ");

	net_debugaddr(&net->netmask_ipv4, NET_ADDR_IPV4);
	debug("/%i", net->netmask_ipv4_len);

	debug(" (max outgoing: TCP=%i, UDP=%i)\n",
	    MAX_TCP_CONNECTIONS, MAX_UDP_CONNECTIONS);

	debug_indentation(iadd);

	debug("simulated gateway: ");
	net_debugaddr(&net->gateway_ipv4_addr, NET_ADDR_IPV4);
	debug(" (");
	net_debugaddr(&net->gateway_ethernet_addr, NET_ADDR_ETHERNET);
	debug(")\n");

	debug_indentation(iadd);
	if (!net->nameserver_known) {
		debug("(could not determine nameserver)");
	} else {
		debug("using nameserver ");
		net_debugaddr(&net->nameserver_ipv4, NET_ADDR_IPV4);
	}
	if (net->domain_name != NULL && net->domain_name[0])
		debug(", domain \"%s\"", net->domain_name);
	debug("\n");
	debug_indentation(-iadd);

	rnp = net->remote_nets;
	if (net->local_port != 0)
		debug("distributed network: local port = %i\n",
		    net->local_port);
	debug_indentation(iadd);
	while (rnp != NULL) {
		debug("remote \"%s\": ", rnp->name);
		net_debugaddr(&rnp->ipv4_addr, NET_ADDR_IPV4);
		debug(" port %i\n", rnp->portnr);
		rnp = rnp->next;
	}
	debug_indentation(-iadd);

	debug_indentation(-iadd);
}


/*
 *  net_init():
 *
 *  This function creates a network, and returns a pointer to it.
 *  ipv4addr should be something like "10.0.0.0", netipv4len = 8.
 *  If n_remote is more than zero, remote should be a pointer to an array
 *  of strings of the following format: "host:portnr".
 *
 *  On failure, exit() is called.
 */
struct net *net_init(struct emul *emul, int init_flags,
	char *ipv4addr, int netipv4len, char **remote, int n_remote,
	int local_port)
{
	struct net *net;
	int res;

	net = malloc(sizeof(struct net));
	if (net == NULL) {
		fprintf(stderr, "net_init(): out of memory\n");
		exit(1);
	}

	memset(net, 0, sizeof(struct net));

	/*  Set the back pointer:  */
	net->emul = emul;

	/*  Sane defaults:  */
	net->timestamp = 0;
	net->first_ethernet_packet = net->last_ethernet_packet = NULL;

#ifdef HAVE_INET_PTON
	res = inet_pton(AF_INET, ipv4addr, &net->netmask_ipv4);
#else
	res = inet_aton(ipv4addr, &net->netmask_ipv4);
#endif
	if (res < 1) {
		fprintf(stderr, "net_init(): could not parse IPv4 address"
		    " '%s'\n", ipv4addr);
		exit(1);
	}

	if (netipv4len < 1 || netipv4len > 30) {
		fprintf(stderr, "net_init(): extremely weird ipv4 "
		    "network length (%i)\n", netipv4len);
		exit(1);
	}
	net->netmask_ipv4_len = netipv4len;

	net->nameserver_known = 0;
	net->domain_name = "";
	parse_resolvconf(net);

	/*  Distributed network? Then add remote hosts:  */
	if (local_port != 0) {
		struct sockaddr_in si_self;

		net->local_port = local_port;
		net->local_port_socket = socket(AF_INET, SOCK_DGRAM, 0);
		if (net->local_port_socket < 0) {
			perror("socket");
			exit(1);
		}

		memset((char *)&si_self, sizeof(si_self), 0);
		si_self.sin_family = AF_INET;
		si_self.sin_port = htons(local_port);
		si_self.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(net->local_port_socket, (struct sockaddr *)&si_self,
		    sizeof(si_self)) < 0) {
			perror("bind");
			exit(1);
		}

		/*  Set the socket to non-blocking:  */
		res = fcntl(net->local_port_socket, F_GETFL);
		fcntl(net->local_port_socket, F_SETFL, res | O_NONBLOCK);
	}
	if (n_remote != 0) {
		struct remote_net *rnp;
		while ((n_remote--) != 0) {
			struct hostent *hp;

			/*  debug("adding '%s'\n", remote[n_remote]);  */
			rnp = malloc(sizeof(struct remote_net));
			memset(rnp, 0, sizeof(struct remote_net));

			rnp->next = net->remote_nets;
			net->remote_nets = rnp;

			rnp->name = strdup(remote[n_remote]);
			if (strchr(rnp->name, ':') != NULL)
				strchr(rnp->name, ':')[0] = '\0';

			hp = gethostbyname(rnp->name);
			if (hp == NULL) {
				fprintf(stderr, "could not resolve '%s'\n",
				    rnp->name);
				exit(1);
			}
			memcpy(&rnp->ipv4_addr, hp->h_addr, hp->h_length);
			free(rnp->name);

			/*  And again:  */
			rnp->name = strdup(remote[n_remote]);
			if (strchr(rnp->name, ':') == NULL) {
				fprintf(stderr, "Remote network '%s' is not "
				    "'host:portnr'?\n", rnp->name);
				exit(1);
			}
			rnp->portnr = atoi(strchr(rnp->name, ':') + 1);
		}
	}

	if (init_flags & NET_INIT_FLAG_GATEWAY)
		net_gateway_init(net);

	net_dumpinfo(net);

	/*  This is necessary when using the real network:  */
	signal(SIGPIPE, SIG_IGN);

	return net;
}


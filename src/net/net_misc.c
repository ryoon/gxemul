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
 *  $Id: net_misc.c,v 1.2 2006-09-04 02:32:34 debug Exp $
 *
 *  Misc. helper functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "machine.h"
#include "misc.h"
#include "net.h"


/*
 *  net_debugaddr():
 *
 *  Print an address using debug().
 */
void net_debugaddr(void *addr, int type)
{
	int i;
	unsigned char *p = addr;

	switch (type) {

	case NET_ADDR_IPV4:
		for (i=0; i<4; i++)
			debug("%s%i", i? "." : "", p[i]);
		break;

	case NET_ADDR_IPV6:
		for (i=0; i<16; i+=2)
			debug("%s%4x", i? ":" : "", p[i] * 256 + p[i+1]);
		break;

	case NET_ADDR_ETHERNET:
		for (i=0; i<6; i++)
			debug("%s%02x", i? ":" : "", p[i]);
		break;

	default:
		fatal("( net_debugaddr(): UNIMPLEMTED type %i )\n", type);
	}
}


/*
 *  net_generate_unique_mac():
 *
 *  Generate a "unique" serial number for a machine. The machine's serial
 *  number is combined with the machine's current number of NICs to form a
 *  more-or-less valid MAC address.
 *
 *  The return value (6 bytes) are written to macbuf.
 */
void net_generate_unique_mac(struct machine *machine, unsigned char *macbuf)
{
	int x, y;

	if (macbuf == NULL || machine == NULL) {
		fatal("**\n**  net_generate_unique_mac(): NULL ptr\n**\n");
		return;
	}

	x = machine->serial_nr;
	y = machine->nr_of_nics;

	macbuf[0] = 0x10;
	macbuf[1] = 0x20;
	macbuf[2] = 0x30;
	macbuf[3] = 0;
	macbuf[4] = 0;
	/*  NOTE/TODO: This only allows 8 nics per machine!  */
	macbuf[5] = (machine->serial_nr << 4) + (machine->nr_of_nics << 1);

	if (macbuf[0] & 1 || macbuf[5] & 1) {
		fatal("Internal error in net_generate_unique_mac().\n");
		exit(1);
	}

	/*  TODO: Remember the mac addresses somewhere?  */
	machine->nr_of_nics ++;
}


/*
 *  net_ip_checksum():
 *
 *  Fill in an IP header checksum. (This works for ICMP too.)
 *  chksumoffset should be 10 for IP headers, and len = 20.
 *  For ICMP packets, chksumoffset = 2 and len = length of the ICMP packet.
 */
void net_ip_checksum(unsigned char *ip_header, int chksumoffset, int len)
{
	int i;
	uint32_t sum = 0;

	for (i=0; i<len; i+=2)
		if (i != chksumoffset) {
			uint16_t w = (ip_header[i] << 8) + ip_header[i+1];
			sum += w;
			while (sum > 65535) {
				int to_add = sum >> 16;
				sum = (sum & 0xffff) + to_add;
			}
		}

	sum ^= 0xffff;
	ip_header[chksumoffset + 0] = sum >> 8;
	ip_header[chksumoffset + 1] = sum & 0xff;
}


/*
 *  net_ip_tcp_checksum():
 *
 *  Fill in a TCP header checksum. This differs slightly from the IP
 *  checksum. The checksum is calculated on a pseudo header, the actual
 *  TCP header, and the data.  This is what the pseudo header looks like:
 *
 *	uint32_t srcaddr;
 *	uint32_t dstaddr;
 *	uint16_t protocol; (= 6 for tcp)
 *	uint16_t tcp_len;
 *
 *  tcp_len is length of header PLUS data.  The psedo header is created
 *  internally here, and does not need to be supplied by the caller.
 */
void net_ip_tcp_checksum(unsigned char *tcp_header, int chksumoffset,
	int tcp_len, unsigned char *srcaddr, unsigned char *dstaddr,
	int udpflag)
{
	int i, pad = 0;
	unsigned char pseudoh[12];
	uint32_t sum = 0;

	memcpy(pseudoh + 0, srcaddr, 4);
	memcpy(pseudoh + 4, dstaddr, 4);
	pseudoh[8] = 0x00;
	pseudoh[9] = udpflag? 17 : 6;
	pseudoh[10] = tcp_len >> 8;
	pseudoh[11] = tcp_len & 255;

	for (i=0; i<12; i+=2) {
		uint16_t w = (pseudoh[i] << 8) + pseudoh[i+1];
		sum += w;
		while (sum > 65535) {
			int to_add = sum >> 16;
			sum = (sum & 0xffff) + to_add;
		}
	}

	if (tcp_len & 1) {
		tcp_len ++;
		pad = 1;
	}

	for (i=0; i<tcp_len; i+=2)
		if (i != chksumoffset) {
			uint16_t w;
			if (!pad || i < tcp_len-2)
				w = (tcp_header[i] << 8) + tcp_header[i+1];
			else
				w = (tcp_header[i] << 8) + 0x00;
			sum += w;
			while (sum > 65535) {
				int to_add = sum >> 16;
				sum = (sum & 0xffff) + to_add;
			}
		}

	sum ^= 0xffff;
	tcp_header[chksumoffset + 0] = sum >> 8;
	tcp_header[chksumoffset + 1] = sum & 0xff;
}


/*
 *  send_udp():
 *
 *  Send a simple UDP packet to a real (physical) host. Used for distributed
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


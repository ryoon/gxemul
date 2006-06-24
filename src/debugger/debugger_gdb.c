/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: debugger_gdb.c,v 1.12 2006-06-24 19:52:28 debug Exp $
 *
 *  Routines used for communicating with the GNU debugger, using the GDB
 *  remote serial protocol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

#include "cpu.h"
#include "debugger.h"
#include "debugger_gdb.h"
#include "machine.h"
#include "memory.h"


extern int single_step;
extern int exit_debugger;


/*
 *  debugger_gdb_listen():
 *
 *  Set up a GDB remote listening port for a specific emulated machine.
 */
static void debugger_gdb_listen(struct machine *machine)
{
	int listen_socket, res;
	struct sockaddr_in si;
	struct sockaddr_in incoming;
	socklen_t incoming_len;

	printf("----------------------------------------------------------"
	    "---------------------\nWaiting for incoming remote GDB connection"
	    " on port %i...\n", machine->gdb.port);

	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket < 0) {
		perror("socket");
		exit(1);
	}

	memset((char *)&si, sizeof(si), 0);
	si.sin_family = AF_INET;
	si.sin_port = htons(machine->gdb.port);
	si.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listen_socket, (struct sockaddr *)&si, sizeof(si)) < 0) {
		perror("bind");
		exit(1);
	}

	if (listen(listen_socket, 1) != 0) {
		perror("listen");
	}

	machine->gdb.socket = accept(listen_socket,
	    (struct sockaddr *)&incoming, &incoming_len);
	printf("Connected; GDB socket = %i\n", machine->gdb.socket);

	/*  Set the socket to non-blocking:  */
	res = fcntl(machine->gdb.socket, F_GETFL);
	fcntl(machine->gdb.socket, F_SETFL, res | O_NONBLOCK);
}


/*
 *  send_packet():
 *
 *  Sends a packet with the correct checksum.
 */
static void send_packet(struct machine *machine, char *msg)
{
	unsigned char hex[17] = "0123456789abcdef";
	unsigned char checksum = 0x00;
	int i = 0;
	unsigned char ch;

	while (msg[i]) {
		checksum += (unsigned char) msg[i];
		i ++;
	}

	ch = '$'; write(machine->gdb.socket, &ch, 1);
	write(machine->gdb.socket, msg, i);
	ch = '#'; write(machine->gdb.socket, &ch, 1);
	ch = hex[checksum >> 4]; write(machine->gdb.socket, &ch, 1);
	ch = hex[checksum & 15]; write(machine->gdb.socket, &ch, 1);
}


/*
 *  debugger_gdb__execute_command():
 *
 *  Execute the command in the machine's receive buffer.
 */
void debugger_gdb__execute_command(struct machine *machine)
{
	char *cmd = (char *) machine->gdb.rx_buf;

	fatal("[ Remote GDB command: '%s' ]\n", machine->gdb.rx_buf);

	if (strcmp(cmd, "?") == 0) {
		send_packet(machine, "S00");
	} else if (strcmp(cmd, "g") == 0 || strncmp(cmd, "p", 1) == 0) {
		char *reply = cpu_gdb_stub(machine->cpus[0], cmd);
		if (reply != NULL) {
			send_packet(machine, reply);
			free(reply);
		}
	} else if (strcmp(cmd, "c") == 0) {
		send_packet(machine, "OK");
		exit_debugger = 1;
	} else if (strncmp(cmd, "Hc", 2) == 0) {
		fatal("[ TODO: GDB SET THREAD ]\n");
		send_packet(machine, "OK");
	} else if (strncmp(cmd, "m", 1) == 0) {
		/*  Memory read  */
		char *p = strchr(cmd, ',');
		if (p == NULL) {
			send_packet(machine, "E00");
		} else {
			uint64_t addr = strtoull(cmd + 1, NULL, 16);
			uint64_t len = strtoull(p + 1, NULL, 16);
			char *reply = malloc(len * 2 + 1);
			size_t i;

			reply[0] = '\0';
			for (i=0; i<len; i++) {
				unsigned char ch;
				machine->cpus[0]->memory_rw(machine->cpus[0],
				    machine->cpus[0]->mem, addr+i, &ch,
				    sizeof(ch), MEM_READ, CACHE_NONE
				    | NO_EXCEPTIONS);
				snprintf(reply + strlen(reply),
				    len*2, "%02x", ch);
			}

			send_packet(machine, reply);
			free(reply);
		}
	} else if (strcmp(cmd, "s") == 0) {
		unsigned char ch = '+';
		write(machine->gdb.socket, &ch, 1);
		exit_debugger = -1;
	} else {
		fatal("[ (UNKNOWN COMMAND) ]\n");
		send_packet(machine, "");
	}
}


/*
 *  debugger_gdb__check_incoming_char():
 *
 *  Handle each incoming character.
 */
int debugger_gdb__check_incoming_char(struct machine *machine)
{
	/*  int old_state = machine->gdb.rx_state;  */
	unsigned char ch, ch1;
	ssize_t len = read(machine->gdb.socket, &ch, 1);

	if (len == 0) {
		perror("GDB socket read");
		fprintf(stderr, "Connection closed. Exiting.\n");
		exit(1);
	}

	/*  EAGAIN, and similar:  */
	if (len < 0)
		return 0;

	/*  debug("[ debugger_gdb: received char ");
	if (ch >= ' ')
		debug("'%c' ]\n", ch);
	else
		debug("0x%02x ]\n", ch);  */

	switch (machine->gdb.rx_state) {

	case RXSTATE_WAITING_FOR_DOLLAR:
		if (ch == '$') {
			machine->gdb.rx_state = RXSTATE_WAITING_FOR_HASH;
			if (machine->gdb.rx_buf != NULL)
				free(machine->gdb.rx_buf);
			machine->gdb.rx_buf_size = 200;
			machine->gdb.rx_buf = malloc(
			    machine->gdb.rx_buf_size + 1);
			machine->gdb.rx_buf_curlen = 0;
			machine->gdb.rx_buf_checksum = 0x00;
		} else if (ch == 0x03) {
			fatal("[ GDB break ]\n");
			single_step = ENTER_SINGLE_STEPPING;
			ch = '+';
			write(machine->gdb.socket, &ch, 1);
			send_packet(machine, "S02");
			machine->gdb.rx_state = RXSTATE_WAITING_FOR_DOLLAR;
		} else {
			if (ch != '+')
				debug("[ debugger_gdb: ignoring char '"
				    "%c' ]\n", ch);
		}
		break;

	case RXSTATE_WAITING_FOR_HASH:
		if (ch == '#') {
			machine->gdb.rx_state = RXSTATE_WAITING_FOR_CHECKSUM1;

			machine->gdb.rx_buf[machine->gdb.rx_buf_curlen] = '\0';
		} else {
			if (machine->gdb.rx_buf_curlen >=
			    machine->gdb.rx_buf_size) {
				machine->gdb.rx_buf_size *= 2;
				machine->gdb.rx_buf = realloc(
				    machine->gdb.rx_buf,
				    machine->gdb.rx_buf_size + 1);
			}

			machine->gdb.rx_buf[
			    machine->gdb.rx_buf_curlen ++] = ch;

			machine->gdb.rx_buf_checksum += ch;

			/*  debug("[ debugger_gdb: current checksum = "
			    "0x%02x ]\n", machine->gdb.rx_buf_checksum);  */
		}
		break;

	case RXSTATE_WAITING_FOR_CHECKSUM1:
		machine->gdb.rx_checksum1 = ch;
		machine->gdb.rx_state = RXSTATE_WAITING_FOR_CHECKSUM2;
		break;

	case RXSTATE_WAITING_FOR_CHECKSUM2:
		ch1 = machine->gdb.rx_checksum1;

		if (ch1 >= '0' && ch1 <= '9')
			ch1 = ch1 - '0';
		else if (ch1 >= 'a' && ch1 <= 'f')
			ch1 = 10 + ch1 - 'a';
		else if (ch1 >= 'A' && ch1 <= 'F')
			ch1 = 10 + ch1 - 'A';

		if (ch >= '0' && ch <= '9')
			ch = ch - '0';
		else if (ch >= 'a' && ch <= 'f')
			ch = 10 + ch - 'a';
		else if (ch >= 'A' && ch <= 'F')
			ch = 10 + ch - 'A';

		if (machine->gdb.rx_buf_checksum != ch1 * 16 + ch) {
			/*  Checksum mismatch!  */

			fatal("[ debugger_gdb: CHECKSUM MISMATCH! (0x%02x, "
			    " calculated is 0x%02x ]\n", ch1 * 16 + ch,
			    machine->gdb.rx_buf_checksum);

			/*  Send a NACK message:  */
			ch = '-';
			write(machine->gdb.socket, &ch, 1);
		} else {
			/*  Checksum is ok. Send an ACK message...  */
			ch = '+';
			write(machine->gdb.socket, &ch, 1);

			/*  ... and execute the command:  */
			debugger_gdb__execute_command(machine);
		}

		machine->gdb.rx_state = RXSTATE_WAITING_FOR_DOLLAR;
		break;

	default:fatal("debugger_gdb_check_incoming(): internal error "
		    "(state %i unknown)\n", machine->gdb.rx_state);
		exit(1);
	}

	/*  if (machine->gdb.rx_state != old_state)
		debug("[ debugger_gdb: state %i -> %i ]\n",
		    old_state, machine->gdb.rx_state);  */

	return 1;
}


/*
 *  debugger_gdb_check_incoming():
 *
 *  This function should be called regularly, to check for incoming data on
 *  the remote GDB socket.
 */
void debugger_gdb_check_incoming(struct machine *machine)
{
	while (debugger_gdb__check_incoming_char(machine))
		;
}


/*
 *  debugger_gdb_after_singlestep():
 *
 *  Single-step works like this:
 *
 *	GDB to GXemul:		s
 *	GXemul to GDB:		+
 *	(GXemul single-steps one instruction)
 *	GXemul to GDB:		T00
 *
 *  This function should be called after the instruction has been executed.
 */
void debugger_gdb_after_singlestep(struct machine *machine)
{
	send_packet(machine, "T00");
}


/*
 *  debugger_gdb_init():
 *
 *  Initialize stuff needed for a GDB remote connection.
 */
void debugger_gdb_init(struct machine *machine)
{
	if (machine->gdb.port < 1)
		return;

	debugger_gdb_listen(machine);
}


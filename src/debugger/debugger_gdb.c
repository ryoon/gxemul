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
 *  $Id: debugger_gdb.c,v 1.3 2006-03-26 19:29:22 debug Exp $
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

#include "debugger_gdb.h"
#include "machine.h"
#include "memory.h"


/*
 *  rx_one():
 *
 *  Helper function.
 */
static unsigned char rx_one(struct machine *machine)
{
	unsigned char ch;

	if (machine->gdb.rx_buf_head == machine->gdb.rx_buf_tail)
		return 0;

	ch = machine->gdb.rx_buf[machine->gdb.rx_buf_tail ++];
	if (machine->gdb.rx_buf_tail == DEBUGGER_GDB_RXBUF_SIZE)
		machine->gdb.rx_buf_tail = 0;

	return ch;
}


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

	machine->gdb.rx_buf = zeroed_alloc(DEBUGGER_GDB_RXBUF_SIZE);
	machine->gdb.rx_buf_head = machine->gdb.rx_buf_tail = 0;
}


/*
 *  debugger_gdb_check_incoming():
 *
 *  This function should be called regularly, to check for incoming data on
 *  the remote GDB socket.
 */
void debugger_gdb_check_incoming(struct machine *machine)
{
	ssize_t len, to_read;
	unsigned char ch;

	to_read = DEBUGGER_GDB_RXBUF_SIZE - machine->gdb.rx_buf_head;
	if (to_read > DEBUGGER_GDB_RXBUF_SIZE / 2)
		to_read = DEBUGGER_GDB_RXBUF_SIZE / 2;

	len = read(machine->gdb.socket, machine->gdb.rx_buf +
	    machine->gdb.rx_buf_head, to_read);
	if (len == 0) {
		perror("GDB socket read");
		fprintf(stderr, "Connection closed.\n");
		exit(1);
	}

	/*  EAGAIN, and similar:  */
	if (len < 0)
		return;

	machine->gdb.rx_buf_head += len;
	if (machine->gdb.rx_buf_head == DEBUGGER_GDB_RXBUF_SIZE)
		machine->gdb.rx_buf_head = 0;

	switch (machine->gdb.rx_state) {

	case RXSTATE_WAITING_FOR_DOLLAR:
		ch = rx_one(machine);
		if (ch == '$')
			machine->gdb.rx_state = RXSTATE_WAITING_FOR_HASH;
		break;

	default:fatal("debugger_gdb_check_incoming(): internal error (state"
		    " %i unknown)\n", machine->gdb.rx_state);
		exit(1);
	}
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


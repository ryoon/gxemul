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
 *  $Id: debugger_gdb.c,v 1.2 2006-03-25 21:24:31 debug Exp $
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

#include "debugger_gdb.h"
#include "machine.h"
#include "memory.h"


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
	machine->gdb.rx_buf_size = DEBUGGER_GDB_RXBUF_SIZE;
	machine->gdb.rx_buf_pos = 0;
}


/*
 *  debugger_gdb_check_incoming():
 *
 *  This function should be called regularly, to check for incoming data on
 *  the remote GDB socket.
 */
void debugger_gdb_check_incoming(struct machine *machine)
{
	switch (machine->gdb.rx_state) {

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


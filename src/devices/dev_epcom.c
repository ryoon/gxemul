/*
 *  Copyright (C) 2006-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_epcom.c,v 1.2 2006-12-30 13:30:57 debug Exp $
 *  
 *  EPCOM serial controller, used by (at least) the TS7200 emulation mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "epcomreg.h"


#define debug fatal

#define	TICK_SHIFT		14
#define	DEV_EPCOM_LENGTH	0x108

struct epcom_data {
	int		irqnr;
	int		console_handle;
	char		*name;
};


/*
 *  dev_epcom_tick():
 */
void dev_epcom_tick(struct cpu *cpu, void *extra)
{
	/*  struct epcom_data *d = extra;  */
}


DEVICE_ACCESS(epcom)
{
	uint64_t idata = 0, odata=0;
	size_t i;
	struct epcom_data *d = extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case EPCOM_Data:
		if (writeflag == MEM_WRITE)
			console_putchar(d->console_handle, idata);
		else
			fatal("TODO: epcom read\n");
		break;

	case EPCOM_Flag:
		if (writeflag == MEM_READ)
			odata = Flag_CTS | Flag_DSR | Flag_DCD | Flag_TXFE;
		else
			fatal("TODO: epcom flag write?\n");
		break;

	default:if (writeflag == MEM_READ) {
			debug("[ epcom (%s): read from reg %i ]\n",
			    d->name, (int)relative_addr);
		} else {
			debug("[ epcom (%s): write to reg %i:",
			    d->name, (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(epcom)
{
	struct epcom_data *d = malloc(sizeof(struct epcom_data));
	size_t nlen;
	char *name;

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct epcom_data));
	d->irqnr	= devinit->irq_nr;
	d->name		= devinit->name2 != NULL? devinit->name2 : "";
	d->console_handle =
	    console_start_slave(devinit->machine, devinit->name2 != NULL?
	    devinit->name2 : devinit->name, devinit->in_use);

	nlen = strlen(devinit->name) + 10;
	if (devinit->name2 != NULL)
		nlen += strlen(devinit->name2);
	name = malloc(nlen);
	if (name == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	if (devinit->name2 != NULL && devinit->name2[0])
		snprintf(name, nlen, "%s [%s]", devinit->name, devinit->name2);
	else
		snprintf(name, nlen, "%s", devinit->name);

	memory_device_register(devinit->machine->memory, name, devinit->addr,
	    DEV_EPCOM_LENGTH, dev_epcom_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(devinit->machine,
	    dev_epcom_tick, d, TICK_SHIFT, 0.0);

	/*
	 *  NOTE:  Ugly cast into a pointer, because this is a convenient way
	 *         to return the console handle to code in src/machine.c.
	 */
	devinit->return_ptr = (void *)(size_t)d->console_handle;

	return 1;
}


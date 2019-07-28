/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: VoCore Palmbus
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "console.h"
#include "device.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_PALMBUS_LENGTH		0x1000

struct palmbus_data {
	// struct interrupt irq;
	int console_handle;
};


DEVICE_ACCESS(palmbus)
{
	struct palmbus_data *d = (struct palmbus_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0x0000:
		odata = 0x30335452;
		break;
	
	case 0x0004:
		odata = 0x20203235;
		break;

	case 0x000c:
		/*  Linux boot message says "SoC Type: Ralink RT5350 id:1 rev:3"  */
		odata = 0x00000103;
		break;

	case 0x0c04:
		console_putchar(d->console_handle, idata);
		break;

	case 0x0c1c:
		odata = 0x20;	// Serial ready (?)
		break;

	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ palmbus: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n",
			    (int)relative_addr, (int)idata);
		} else {
			fatal("[ palmbus: unimplemented read from address 0x%x "
			    "]\n", (int)relative_addr);
		}
		/*  exit(1);  */
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(palmbus)
{
	char *name2;
	struct palmbus_data *d;

	CHECK_ALLOCATION(d = (struct palmbus_data *) malloc(sizeof(struct palmbus_data)));
	memset(d, 0, sizeof(struct palmbus_data));

	// INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	d->console_handle = console_start_slave(devinit->machine, "uartlite", 1);

	memory_device_register(devinit->machine->memory, name2,
	    devinit->addr, DEV_PALMBUS_LENGTH,
	    dev_palmbus_access, (void *)d, DM_DEFAULT, NULL);

	return 1;
}


/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sn.c,v 1.8 2005-02-21 07:01:08 debug Exp $
 *  
 *  National Semiconductor SONIC ("sn") DP83932 ethernet.
 *
 *
 *  TODO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "net.h"

#include "dp83932reg.h"


struct sn_data {
	int		irq_nr;
	unsigned char	macaddr[6];
	uint32_t	reg[SONIC_NREGS];
};


/*
 *  dev_sn_access():
 */
int dev_sn_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct sn_data *d = (struct sn_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	if (regnr < SONIC_NREGS) {
		if (writeflag == MEM_WRITE)
			d->reg[regnr] = idata;
		else
			odata = d->reg[regnr];
	}

	switch (regnr) {
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ sn: unimplemented write to address 0x%x"
			    " (regnr %i), data=0x%02x ]\n",
			    (int)relative_addr, regnr, (int)idata);
		} else {
			fatal("[ sn: unimplemented read from address 0x%x "
			    "(regnr %i) ]\n", (int)relative_addr, regnr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sn_init():
 */
void dev_sn_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr, int irq_nr)
{
	char *name2;
	struct sn_data *d = malloc(sizeof(struct sn_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sn_data));
	d->irq_nr = irq_nr;
	net_generate_unique_mac(d->macaddr);

	name2 = malloc(50);
	if (name2 == NULL) {
		fprintf(stderr, "out of memory in dev_sn_init()\n");
		exit(1);
	}
	sprintf(name2, "sn [%02x:%02x:%02x:%02x:%02x:%02x]",
	    d->macaddr[0], d->macaddr[1], d->macaddr[2],
	    d->macaddr[3], d->macaddr[4], d->macaddr[5]);

	memory_device_register(mem, name2, baseaddr, DEV_SN_LENGTH,
	    dev_sn_access, (void *)d, MEM_DEFAULT, NULL);

	net_add_nic(cpu->machine->emul->net, d, d->macaddr);
}


/*
 *  Copyright (C) 2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_m8820x.c,v 1.3 2007-05-17 02:00:30 debug Exp $
 *
 *  M88200/M88204 CMMU (Cache/Memory Management Unit)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#include "m8820x.h"

struct m8820x_data {
	int		cmmu_nr;
};


/*
 *  m8820x_command():
 *
 *  Handle M8820x commands written to the System Command Register.
 */
static void m8820x_command(struct cpu *cpu, struct m8820x_data *d)
{
	uint32_t *regs = cpu->cd.m88k.cmmu[d->cmmu_nr]->reg;
	int cmd = regs[CMMU_SCR];

	switch (cmd) {

	case CMMU_FLUSH_CACHE_CB_LINE:
	case CMMU_FLUSH_CACHE_INV_LINE:
	case CMMU_FLUSH_CACHE_INV_ALL:
	case CMMU_FLUSH_CACHE_CBI_LINE:
	case CMMU_FLUSH_CACHE_CBI_PAGE:
	case CMMU_FLUSH_CACHE_CBI_SEGMENT:
	case CMMU_FLUSH_CACHE_CBI_ALL:
		/*  TODO  */
		break;

	case CMMU_FLUSH_USER_ALL:
	case CMMU_FLUSH_USER_PAGE:
	case CMMU_FLUSH_SUPER_ALL:
	case CMMU_FLUSH_SUPER_PAGE:
		/*  TODO: Invalidate translation caches.  */
		break;

	default:
		fatal("[ m8820x_command: FATAL ERROR! unimplemented "
		    "command 0x%02x ]\n", cmd);
		exit(1);
	}
}


DEVICE_ACCESS(m8820x)
{
	uint64_t idata = 0, odata = 0;
	struct m8820x_data *d = extra;
	uint32_t *regs = cpu->cd.m88k.cmmu[d->cmmu_nr]->reg;
	uint32_t *batc = cpu->cd.m88k.cmmu[d->cmmu_nr]->batc;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ)
		odata = regs[relative_addr / sizeof(uint32_t)];

	switch (relative_addr / sizeof(uint32_t)) {

	case CMMU_IDR:
		if (writeflag == MEM_WRITE) {
			fatal("m8820x: write to CMMU_IDR: TODO\n");
			exit(1);
		}
		break;

	case CMMU_SCR:
		if (writeflag == MEM_READ) {
			fatal("m8820x: read from CMMU_SCR: TODO\n");
			exit(1);
		} else {
			regs[relative_addr / sizeof(uint32_t)] = idata;
			m8820x_command(cpu, d);
		}
		break;

	case CMMU_SSR:
		if (writeflag == MEM_WRITE) {
			fatal("m8820x: write to CMMU_SSR: TODO\n");
			exit(1);
		}
		break;

	case CMMU_SAR:
	case CMMU_SCTR:
	case CMMU_SAPR:		/*  TODO: Invalidate something for  */
	case CMMU_UAPR:		/*  SAPR and UAPR writes?  */
		if (writeflag == MEM_WRITE)
			regs[relative_addr / sizeof(uint32_t)] = idata;
		break;

	case CMMU_BWP0:
	case CMMU_BWP1:
	case CMMU_BWP2:
	case CMMU_BWP3:
	case CMMU_BWP4:
	case CMMU_BWP5:
	case CMMU_BWP6:
	case CMMU_BWP7:
		if (writeflag == MEM_WRITE) {
			uint32_t old;

			regs[relative_addr / sizeof(uint32_t)] = idata;

			/*  Also write to the specific batc registers:  */
			old = batc[(relative_addr / sizeof(uint32_t))
			    - CMMU_BWP0];
			batc[(relative_addr / sizeof(uint32_t)) - CMMU_BWP0]
			    = idata;
			if (old != idata) {
				fatal("TODO: invalidate batc translations\n");
				exit(1);
			}
		}
		break;

	case CMMU_CSSP0:
		/*  TODO: Actually care about cache details.  */
		break;

	default:fatal("[ m8820x: unimplemented %s offset 0x%x",
		    writeflag == MEM_WRITE? "write to" : "read from",
		    (int) relative_addr);
		if (writeflag == MEM_WRITE)
			fatal(": 0x%x", (int)idata);
		fatal(" ]\n");
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(m8820x)
{
	struct m8820x_data *d = malloc(sizeof(struct m8820x_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct m8820x_data));

	d->cmmu_nr = devinit->addr2;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, M8820X_LENGTH, dev_m8820x_access, (void *)d,
	    DM_DEFAULT, NULL);

	return 1;
}


/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_mp.c,v 1.24 2005-02-22 12:05:16 debug Exp $
 *
 *  This is a fake multiprocessor (MP) device. It can be useful for
 *  theoretical experiments, but probably bares no resemblance to any
 *  multiprocessor controller used in any real machine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "cpu_mips.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "mp.h"


struct mp_data {
	struct cpu	**cpus;
	uint64_t	startup_addr;
	uint64_t	stack_addr;
	uint64_t	pause_addr;
};


/*
 *  dev_mp_access():
 */
int dev_mp_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct mp_data *d = extra;
	int i, which_cpu;
	uint64_t idata = 0, odata = 0;

        idata = memory_readmax64(cpu, data, len);

	/*
	 *  NOTE: It is up to the user of this device to read or write
	 *  correct addresses. (A write to NCPUS is pretty useless,
	 *  for example.)
	 */

	switch (relative_addr) {

	case DEV_MP_WHOAMI:
		odata = cpu->cpu_id;
		break;

	case DEV_MP_NCPUS:
		odata = cpu->machine->ncpus;
		break;

	case DEV_MP_STARTUPCPU:
		which_cpu = idata;
		d->cpus[which_cpu]->pc = d->startup_addr;
		switch (cpu->machine->arch) {
		case ARCH_MIPS:
			d->cpus[which_cpu]->cd.mips.gpr[MIPS_GPR_SP] =
			    d->stack_addr;
			break;
		case ARCH_PPC:
			d->cpus[which_cpu]->cd.ppc.gpr[1] = d->stack_addr;
			break;
		default:
			fatal("dev_mp(): DEV_MP_STARTUPCPU: not for this"
			    " arch yet!\n");
			exit(1);
		}
		d->cpus[which_cpu]->running = 1;
		/*  debug("[ dev_mp: starting up cpu%i at 0x%llx ]\n", 
		    which_cpu, (long long)d->startup_addr);  */
		break;

	case DEV_MP_STARTUPADDR:
		if (len==4 && (idata >> 32) == 0 && (idata & 0x80000000ULL))
			idata |= 0xffffffff00000000ULL;
		d->startup_addr = idata;
		break;

	case DEV_MP_PAUSE_ADDR:
		d->pause_addr = idata;
		break;

	case DEV_MP_PAUSE_CPU:
		/*  Pause all cpus except our selves:  */
		which_cpu = idata;

		for (i=0; i<cpu->machine->ncpus; i++)
			if (i!=which_cpu)
				d->cpus[i]->running = 0;
		break;

	case DEV_MP_UNPAUSE_CPU:
		/*  Unpause all cpus except our selves:  */
		which_cpu = idata;
		for (i=0; i<cpu->machine->ncpus; i++)
			if (i!=which_cpu)
				d->cpus[i]->running = 1;
		break;

	case DEV_MP_STARTUPSTACK:
		if (len == 4 && (idata >> 32) == 0 && (idata & 0x80000000ULL))
			idata |= 0xffffffff00000000ULL;
		d->stack_addr = idata;
		break;

	case DEV_MP_HARDWARE_RANDOM:
		/*  Return (up to) 64 bits of "hardware random":  */
		odata = random();
		odata = (odata << 31) ^ random();
		odata = (odata << 31) ^ random();
		break;

	case DEV_MP_MEMORY:
		/*
		 *  Return the number of bytes of memory in the system.
		 *
		 *  (It is assumed to be located at physical address 0.
		 *  It is actually located at machine->memory_offset_in_mb
		 *  but that is only used for SGI emulation so far.)
		 */
		odata = cpu->machine->physical_ram_in_mb * 1048576;
		break;

	default:
		fatal("[ dev_mp: unimplemented relative addr 0x%x ]\n",
		    relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_mp_init():
 */
void dev_mp_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr)
{
	struct mp_data *d;
	d = malloc(sizeof(struct mp_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct mp_data));
	d->cpus = machine->cpus;
	d->startup_addr = INITIAL_PC;
	d->stack_addr = INITIAL_STACK_POINTER;

	memory_device_register(mem, "mp", baseaddr, DEV_MP_LENGTH,
	    dev_mp_access, d, MEM_DEFAULT, NULL);
}


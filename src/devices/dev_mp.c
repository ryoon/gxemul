/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: dev_mp.c,v 1.5 2004-02-12 15:55:37 debug Exp $
 *  
 *  Multiprocessor support.  (This is a fake device, only for testing.)
 *
 *  TODO:  Find out how actual MIPS machines implement MP stuff.
 *
 *  TODO 2:  This is really broken and should be fixed some day.
 *  (Experimental stuff)
 */

#include <stdio.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


extern int register_dump;
extern int instruction_trace;
extern int ncpus;


/*
 *  dev_mp_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_mp_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	static uint64_t startup_addr = INITIAL_PC;
	static uint64_t pause_addr;
	int i;
	uint64_t addr = 0;

	addr = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ && relative_addr == DEV_MP_WHOAMI) {
		for (i=0; i<len; i++)
			data[i] = cpu->cpu_id;
		return 1;
	}

	if (writeflag == MEM_READ && relative_addr == DEV_MP_NCPUS) {
		for (i=0; i<len; i++)
			data[i] = ncpus;
		return 1;
	}

	if (writeflag == MEM_WRITE && relative_addr == DEV_MP_STARTUPCPU) {
		int which_cpu = data[0];
		struct cpu **cpus = (struct cpu **) extra;
		cpus[which_cpu]->pc = startup_addr;
		cpus[which_cpu]->running = 1;
		/*  debug("[ dev_mp: starting up cpu%i at 0x%llx ]\n", which_cpu, (long long)startup_addr);  */
		return 1;
	}

	if (writeflag == MEM_WRITE && relative_addr == DEV_MP_STARTUPADDR) {
		if ((addr >> 32) == 0 && (addr & 0x80000000))
			addr |= 0xffffffff00000000;
		startup_addr = addr;
		return 1;
	}

	if (writeflag == MEM_WRITE && relative_addr == DEV_MP_PAUSE_ADDR) {
		pause_addr = addr;
		/*  TODO... hm  */
		return 1;
	}

	if (writeflag == MEM_WRITE && relative_addr == DEV_MP_PAUSE_CPU) {
		/*  Pause all cpus except our selves:  */
		int which_cpu = data[0];

		for (i=0; i<ncpus; i++)
			if (i!=which_cpu) {
				struct cpu **cpus = (struct cpu **) extra;
				cpus[i]->running = 0;
			}
	}

	if (writeflag == MEM_WRITE && relative_addr == DEV_MP_UNPAUSE_CPU) {
		/*  Unpause all cpus except our selves:  */
		int which_cpu = data[0];

		for (i=0; i<ncpus; i++)
			if (i!=which_cpu) {
				struct cpu **cpus = (struct cpu **) extra;
				cpus[i]->running = 1;
			}
	}

	return 0;
}


/*
 *  dev_mp_init():
 */
void dev_mp_init(struct memory *mem, struct cpu *cpus[])
{
	memory_device_register(mem, "mp", DEV_MP_ADDRESS, DEV_MP_LENGTH, dev_mp_access, (void *)cpus);
}


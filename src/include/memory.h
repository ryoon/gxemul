#ifndef	MEMORY_H
#define	MEMORY_H

/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: memory.h,v 1.21 2004-12-15 01:59:57 debug Exp $
 *
 *  Memory controller related functions.
 */

#include <sys/types.h>
#include <inttypes.h>

#include "misc.h"


/*  memory.c:  */
uint64_t memory_readmax64(struct cpu *cpu, unsigned char *buf, int len);
void memory_writemax64(struct cpu *cpu, unsigned char *buf, int len, uint64_t data);

void *zeroed_alloc(size_t s);

struct memory *memory_new(uint64_t physical_max);

int memory_points_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, int min_string_length);
char *memory_conv_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr, char *buf, int bufsize);

unsigned char *memory_paddr_to_hostaddr(struct memory *mem, uint64_t paddr, int writeflag);

/*  memory_fast_v2h.c:  */
unsigned char *fast_vaddr_to_hostaddr(struct cpu *cpu, uint64_t vaddr, int writeflag);

int translate_address_mmu3k(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);
int translate_address_generic(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);

#define FLAG_WRITEFLAG          1
#define FLAG_NOEXCEPTIONS       2
#define FLAG_INSTR              4

int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr, unsigned char *data, size_t len, int writeflag, int cache);
#define	MEMORY_ACCESS_FAILED	0
#define	MEMORY_ACCESS_OK	1

void memory_device_bintrans_access(struct cpu *, struct memory *mem, void *extra, uint64_t *low, uint64_t *high);

void memory_device_register_statefunction(
	struct memory *mem, void *extra,
	int (*dev_f_state)(struct cpu *,
	    struct memory *, void *extra, int wf, int nr,
	    int *type, char **namep, void **data, size_t *len));

void memory_device_register(struct memory *mem, const char *, uint64_t baseaddr, uint64_t len, int (*f)(
	struct cpu *,struct memory *,uint64_t,unsigned char *,size_t,int,void *), void *, int flags, unsigned char *bintrans_data);
#define	MEM_DEFAULT			0
#define	MEM_BINTRANS_OK			1
#define	MEM_BINTRANS_WRITE_OK		2

#endif	/*  MEMORY_H  */


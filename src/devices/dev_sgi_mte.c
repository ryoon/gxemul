/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_sgi_mte.c,v 1.1 2004-01-09 16:25:26 debug Exp $
 *  
 *  SGI "mte". This device seems to be an accelerator for copying/clearing
 *  memory.  Used in SGI-IP32.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#define	ZERO_CHUNK_LEN		4096

struct sgi_mte_data {
	uint64_t	reg[DEV_SGI_MTE_LENGTH / sizeof(uint64_t)];
};


/*
 *  dev_sgi_mte_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_sgi_mte_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct sgi_mte_data *d = (struct sgi_mte_data *) extra;
	uint64_t first_addr, last_addr, zerobuflen, fill_addr, fill_len;
	unsigned char zerobuf[ZERO_CHUNK_LEN];
	uint64_t idata = 0, odata = 0;
	int i, regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint64_t);

	/*  Treat all registers as read/write, by default.  */
	if (writeflag == MEM_WRITE)
		d->reg[regnr] = idata;
	else
		odata = d->reg[regnr];

	/*
	 *  I've not found any docs about this 'mte' device at all, so this is just
	 *  a guess. The mte seems to be used for copying and zeroing chunks of
	 *  memory.
	 *
	 *  [ sgi_mte: unimplemented write to address 0x3030, data=0x00000000003da000 ]  <-- first address
	 *  [ sgi_mte: unimplemented write to address 0x3038, data=0x00000000003f9fff ]  <-- last address
	 *  [ sgi_mte: unimplemented write to address 0x3018, data=0x0000000000000000 ]  <-- what to fill?
	 *  [ sgi_mte: unimplemented write to address 0x3008, data=0x00000000ffffffff ]  <-- ?
	 *  [ sgi_mte: unimplemented write to address 0x3800, data=0x0000000000000011 ]  <-- operation (0x11 = zerofill)
	 *
	 *  [ sgi_mte: unimplemented write to address 0x1700, data=0x80001ea080001ea1 ]  <-- also containing the address to fill (?)
	 *  [ sgi_mte: unimplemented write to address 0x1708, data=0x80001ea280001ea3 ]
	 *  [ sgi_mte: unimplemented write to address 0x1710, data=0x80001ea480001ea5 ]
	 *  ...
	 *  [ sgi_mte: unimplemented write to address 0x1770, data=0x80001e9c80001e9d ]
	 *  [ sgi_mte: unimplemented write to address 0x1778, data=0x80001e9e80001e9f ]
	 */
	switch (relative_addr) {

	/*  No warnings for these:  */
	case 0x3030:
	case 0x3038:
		break;

	/*  Unknown, but no warning:  */
	case 0x4000:
	case 0x3018:
	case 0x3008:
	case 0x1700:
	case 0x1708:
	case 0x1710:
	case 0x1718:
	case 0x1720:
	case 0x1728:
	case 0x1730:
	case 0x1738:
	case 0x1740:
	case 0x1748:
	case 0x1750:
	case 0x1758:
	case 0x1760:
	case 0x1768:
	case 0x1770:
	case 0x1778:
		break;

	/*  Operations:  */
	case 0x3800:
		if (writeflag == MEM_WRITE) {
			switch (idata) {
			case 0x11:		/*  zerofill  */
				first_addr = d->reg[0x3030 / sizeof(uint64_t)];
				last_addr  = d->reg[0x3038 / sizeof(uint64_t)];
				zerobuflen = last_addr - first_addr + 1;
				debug("[ sgi_mte: zerofill: first = 0x%016llx, last = 0x%016llx, length = 0x%llx ]\n",
				    (long long)first_addr, (long long)last_addr, (long long)zerobuflen);

				/*  TODO:  is there a better way to implement this?  */
				memset(zerobuf, 0, sizeof(zerobuf));
				fill_addr = first_addr;
				while (zerobuflen > 0) {
					if (zerobuflen > sizeof(zerobuf))
						fill_len = sizeof(zerobuf);
					else
						fill_len = zerobuflen;
					memory_rw(cpu, mem, fill_addr, zerobuf, fill_len, MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
					fill_addr += fill_len;
					zerobuflen -= sizeof(zerobuf);
				}

				break;
			default:
				fatal("[ sgi_mte: UNKNOWN operation 0x%x ]\n", idata);
			}
		}
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_mte: unimplemented write to address 0x%llx, data=0x%016llx ]\n", (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_mte: unimplemented read from address 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mte_init():
 */
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_mte_data *d = malloc(sizeof(struct sgi_mte_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_mte_data));

	memory_device_register(mem, "sgi_mte", baseaddr, DEV_SGI_MTE_LENGTH, dev_sgi_mte_access, (void *)d);
}


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
 *  $Id: dev_le.c,v 1.8 2004-07-05 01:09:04 debug Exp $
 *  
 *  LANCE ethernet, as used on DECstations.
 *
 *  This is partly based on "PMAD-AA TURBOchannel Ethernet Module Functional
 *  Specification", and partly on studying the NetBSD header file for
 *  this device.
 *
 *  This is what the memory layout looks like on a DECstation:
 *
 *	0x000000 - 0x0fffff	Ethernet SRAM buffer
 *	0x100000 - 0x17ffff	LANCE
 *	0x1c0000 - 0x1fffff	Ethernet Diagnostic ROM and Station
 *				Address ROM
 *
 *  TODO:  Make sure that this device works with both NetBSD and Ultrix
 *         and on as many DECstation models as possible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "if_lereg.h"


/*  #define LE_DEBUG  */


#define	N_REGISTERS	4
#define	SRAM_SIZE	(128*1024)

struct le_data {
	int		irq_nr;

	uint64_t	buf_start;
	uint64_t	buf_end;
	int		len;

	int		reg_select;
	uint16_t	reg[N_REGISTERS];

	unsigned char	sram[SRAM_SIZE];

	/*  Initialization block:  */
	uint32_t	init_block_addr;
	uint16_t	mode;
	uint64_t	padr;
	uint64_t	ladrf;
	uint32_t	rdra;
	uint8_t		rlen;
	uint32_t	tdra;
	uint8_t		tlen;
};


/*
 *  le_read_16bit():
 */
uint64_t le_read_16bit(struct le_data *d, int addr)
{
	/*  TODO: This is for little endian only  */
	int x = d->sram[addr & (SRAM_SIZE-1)] +
	       (d->sram[(addr+1) & (SRAM_SIZE-1)] << 8);
	return x;
}


/*
 *  le_chip_init():
 *
 *  Initialize data structures by reading an 'initialization block' from the
 *  SRAM.
 */
void le_chip_init(struct le_data *d)
{
	d->init_block_addr = (d->reg[1] & 0xffff) + ((d->reg[2] & 0xff) << 16);
	if (d->init_block_addr & 1)
		fatal("[ le: WARNING! initialization block address not word aligned? ]\n");

	fatal("[ le: d->init_block_addr = 0x%06x ]\n", d->init_block_addr);

	d->mode = le_read_16bit(d, d->init_block_addr + 0);
	d->padr = le_read_16bit(d, d->init_block_addr + 2);
	d->padr += (le_read_16bit(d, d->init_block_addr + 4) << 16);
	d->padr += (le_read_16bit(d, d->init_block_addr + 6) << 32);
	d->ladrf = le_read_16bit(d, d->init_block_addr + 8);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 10) << 16);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 12) << 32);
	d->ladrf += (le_read_16bit(d, d->init_block_addr + 14) << 48);
	d->rdra = le_read_16bit(d, d->init_block_addr + 16);
	d->rdra += ((le_read_16bit(d, d->init_block_addr + 18) & 0xff) << 16);
	d->rlen = ((le_read_16bit(d, d->init_block_addr + 18) & 0xff00) >> 8);
	d->tdra = le_read_16bit(d, d->init_block_addr + 20);
	d->tdra += ((le_read_16bit(d, d->init_block_addr + 22) & 0xff) << 16);
	d->tlen = ((le_read_16bit(d, d->init_block_addr + 22) & 0xff00) >> 8);

	fatal("[ le: DEBUG: mode              %04x ]\n", d->mode);
	fatal("[ le: DEBUG: padr  %016llx ]\n", (long long)d->padr);
	fatal("[ le: DEBUG: ladrf %016llx ]\n", (long long)d->ladrf);
	fatal("[ le: DEBUG: rdra            %06llx ]\n", d->rdra);
	fatal("[ le: DEBUG: rlen                %02x ]\n", d->rlen);
	fatal("[ le: DEBUG: tdra            %06llx ]\n", d->tdra);
	fatal("[ le: DEBUG: tlen                %02x ]\n", d->tlen);

	/*  Set IDON and reset the INIT bit when we are done.  */
	d->reg[0] |= LE_IDON;
	d->reg[0] &= ~LE_INIT;
}


/*
 *  le_register_fix():
 */
void le_register_fix(struct le_data *d)
{
	if (d->reg[0] & LE_INIT)
		le_chip_init(d);

	/*  SERR should be the OR of BABL, CERR, MISS, and MERR:  */
	d->reg[0] &= ~LE_SERR;
	if (d->reg[0] & (LE_BABL | LE_CERR | LE_MISS | LE_MERR))
		d->reg[0] |= LE_SERR;

	/*  INTR should be the OR of BABL, MISS, MERR, RINT, TINT, IDON:  */
	d->reg[0] &= ~LE_INTR;
	if (d->reg[0] & (LE_BABL | LE_MISS | LE_MERR | LE_RINT |
	    LE_TINT | LE_IDON))
		d->reg[0] |= LE_SERR;

	/*  The MERR bit clears some bits:  */
	if (d->reg[0] & LE_MERR)
		d->reg[0] &= ~(LE_RXON | LE_TXON);

	/*  The STOP bit clears a lot of stuff:  */
	if (d->reg[0] & LE_STOP)
		d->reg[0] &= ~(LE_SERR | LE_BABL | LE_CERR | LE_MISS | LE_MERR
		    | LE_RINT | LE_TINT | LE_IDON | LE_INTR | LE_INEA
		    | LE_RXON | LE_TXON | LE_TDMD);
}


/*
 *  dev_le_tick():
 */
void dev_le_tick(struct cpu *cpu, void *extra)
{
	struct le_data *d = (struct le_data *) extra;

	le_register_fix(d);

	if (d->reg[0] & LE_INTR && d->reg[0] & LE_INEA)
		cpu_interrupt(cpu, d->irq_nr);
	else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  le_register_write():
 *
 *  This function is called when the value 'x' is written to register 'r'.
 */
void le_register_write(struct le_data *d, int r, uint32_t x)
{
	switch (r) {
	case 0:	/*  CSR0:  */
		/*  Some bits are write-one-to-clear:  */
		if (x & LE_BABL)
			d->reg[r] &= ~LE_BABL;
		if (x & LE_CERR)
			d->reg[r] &= ~LE_CERR;
		if (x & LE_MISS)
			d->reg[r] &= ~LE_MISS;
		if (x & LE_MERR)
			d->reg[r] &= ~LE_MERR;
		if (x & LE_RINT)
			d->reg[r] &= ~LE_TINT;
		if (x & LE_RINT)
			d->reg[r] &= ~LE_TINT;
		if (x & LE_IDON)
			d->reg[r] &= ~LE_IDON;

		/*  Some bits are write-only settable, not clearable:  */
		if (x & LE_TDMD)
			d->reg[r] |= LE_TDMD;
		if (x & LE_STRT) {
			if (!(d->reg[r] & LE_STOP))
				fatal("[ le: attempt to STaRT before STOPped! ]\n");
			d->reg[r] |= LE_STRT;
			d->reg[r] &= ~LE_STOP;
		}
		if (x & LE_INIT) {
			if (!(d->reg[r] & LE_STOP))
				fatal("[ le: attempt to INIT before STOPped! ]\n");
			d->reg[r] |= LE_INIT;
			d->reg[r] &= ~LE_STOP;
		}
		if (x & LE_STOP) {
			d->reg[r] |= LE_STOP;
			/*  STOP takes precedence over STRT and INIT:  */
			d->reg[r] &= ~(LE_STRT | LE_INIT);
		}

		/*  Some bits get through, both settable and clearable:  */
		d->reg[r] &= ~LE_INEA;
		d->reg[r] |= (x & LE_INEA);
		break;

	default:
		/*  CSR1, CSR2, and CSR3:  */
		d->reg[r] = x;
	}
}


/*
 *  dev_le_access():
 */
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i;
	struct le_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

#ifdef LE_DEBUG
	if (writeflag == MEM_WRITE)
		fatal("[ le: write to addr 0x%06x: 0x%08x ]\n",
		    relative_addr, idata);
#endif

	/*  Automatic update of some registers:  */
	le_register_fix(d);

	/*
	 *  Read/write of the SRAM:
	 */
	if (relative_addr < SRAM_SIZE && relative_addr + len <= SRAM_SIZE) {
		if (writeflag == MEM_READ) {
			memcpy(data, d->sram + relative_addr, len);
			fatal("[ le: read from SRAM offset 0x%05x:",
			    relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
		} else {
			memcpy(d->sram + relative_addr, data, len);
			fatal("[ le: write to SRAM offset 0x%05x:",
			    relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
		}
		return 1;
	}


	/*
	 *  Read from station's ROM ethernet address:
	 *  This returns a MAC address of 01:02:03:04:05:06.
	 */
	if (relative_addr >= 0x1c0000 && relative_addr <= 0x1c0017) {
		i = (relative_addr & 0xff) / 4;

		if (writeflag == MEM_READ) {
			odata = (i << 24) + (i << 16) + (i << 8) + i;
		} else {
			fatal("[ le: WRITE to ethernet addr (%08lx):",
			    (long)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" ]\n");
		}

		goto do_return;
	}


	switch (relative_addr) {

	/*  Register read/write:  */
	case 0x100000:
		if (writeflag==MEM_READ) {
			odata = d->reg[d->reg_select];
			fatal("[ le: read from register 0x%02x: 0x%02x ]\n",
			    d->reg_select, (int)odata);

			/*
			 *  A read from csr1..3 should return "undefined"
			 *  result if the stop bit is set.
			 */
#if 0
			if (d->reg_select != 0 && d->reg[0] & LE_STOP) {
				odata = 0x0000;
				fatal("[ le: read from register 0x%02x when STOPped: UNDEFINED ]\n",
				    d->reg_select);
			}
#endif
		} else {
			fatal("[ le: write to register 0x%02x: 0x%02x ]\n",
			    d->reg_select, (int)idata);

			/*
			 *  A write to from csr1..3 when the stop bit is
			 *  set should be ignored.
			 */
#if 0
			if (d->reg_select != 0 && d->reg[0] & LE_STOP) {
				fatal("[ le: write to register 0x%02x when STOPped: IGNORED ]\n",
				    d->reg_select);
			} else
#endif
				le_register_write(d, d->reg_select, idata);
		}
		break;

	/*  Register select:  */
	case 0x100004:
		if (writeflag==MEM_READ) {
			odata = d->reg_select;
			debug("[ le: read from register select: 0x%02x ]\n",
			    (int)odata);
		} else {
			debug("[ le: write to register select: 0x%02x ]\n",
			    (int)idata);
			d->reg_select = idata & (N_REGISTERS - 1);
			if (idata >= N_REGISTERS)
				fatal("[ le: WARNING! register select %i (max is %i) ]\n",
				    idata, N_REGISTERS - 1);
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			fatal("[ le: read from UNIMPLEMENTED addr 0x%06x ]\n",
			    relative_addr);
		} else {
			fatal("[ le: write to UNIMPLEMENTED addr 0x%06x: 0x%08x ]\n",
			    relative_addr, (int)idata);
		}
	}


do_return:

	if (writeflag == MEM_READ) {
		memory_writemax64(cpu, data, len, odata);
#ifdef LE_DEBUG
		fatal("[ le: read from addr 0x%06x: 0x%08x ]\n",
		    relative_addr, odata);
#endif
	}

	dev_le_tick(cpu, extra);

	return 1;
}


/*
 *  dev_le_init():
 */
void dev_le_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	uint64_t buf_start, uint64_t buf_end, int irq_nr, int len)
{
	struct le_data *d = malloc(sizeof(struct le_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(d, 0, sizeof(struct le_data));
	d->irq_nr    = irq_nr;
	d->len       = len;

	/*  TODO:  Are these actually used yet?  */
	d->buf_start = buf_start;
	d->buf_end   = buf_end;

	/*  Initial register contents:  */
	d->reg[0] = LE_STOP;

	memory_device_register(mem, "le", baseaddr, len, dev_le_access,
	    (void *)d);

	cpu_add_tickfunction(cpu, dev_le_tick, d, 10);
}


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
 *  $Id: dev_jazz.c,v 1.4 2005-01-04 16:45:22 debug Exp $
 *  
 *  Microsoft Jazz-related stuff (Acer PICA-61, etc).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "pica.h"
#include "jazz_r4030_dma.h"


#define	DEV_JAZZ_TICKSHIFT		14

#define	PICA_TIMER_IRQ			15


/*
 *  dev_jazz_dma_controller():
 */
size_t dev_jazz_dma_controller(void *dma_controller_data,
	unsigned char *data, size_t len, int writeflag)
{
	struct jazz_data *d = (struct jazz_data *) dma_controller_data;
	struct cpu *cpu = d->cpu;
	int i, enab_writeflag;
	int res;
	uint32_t dma_addr;
	unsigned char tr[sizeof(uint32_t)];
	uint32_t phys_addr;

#if 0
	fatal("[ dev_jazz_dma_controller(): writeflag=%i, len=%i, data =",
	    writeflag, (int)len);
	for (i=0; i<len; i++)
		fatal(" %02x", data[i]);
	fatal(" mode=%08x enable=%08x count=%08x addr=%08x",
	    d->dma0_mode, d->dma0_enable, d->dma0_count, d->dma0_addr);
	fatal(" table=%08x",
	    d->dma_translation_table_base);
	fatal(" ]\n");
#endif

	if (!(d->dma0_enable & R4030_DMA_ENAB_RUN)) {
		fatal("[ dev_jazz_dma_controller(): dma not enabled? ]\n");
		/*  return 0;  */
	}

	/*  R4030 "write" means write to the device, writeflag as the
	    argument to this function means write to memory.  */
	enab_writeflag = (d->dma0_enable & R4030_DMA_ENAB_WRITE)? 0 : 1;
	if (enab_writeflag != writeflag) {
		fatal("[ dev_jazz_dma_controller(): wrong direction? ]\n");
		return 0;
	}

	dma_addr = d->dma0_addr;
	i = 0;
	while (dma_addr < d->dma0_addr + d->dma0_count && i < len) {

		res = memory_rw(cpu, cpu->mem, d->dma_translation_table_base +
		    (dma_addr >> 12) * 8,
		    tr, sizeof(tr), 0, PHYSICAL | NO_EXCEPTIONS);

		if (cpu->byte_order==EMUL_BIG_ENDIAN)
			phys_addr = (tr[0] << 24) + (tr[1] << 16) +
			    (tr[2] << 8) + tr[3];
		else
			phys_addr = (tr[3] << 24) + (tr[2] << 16) +
			    (tr[1] << 8) + tr[0];
		phys_addr &= ~0xfff;	/*  just in case  */
		phys_addr += (dma_addr & 0xfff);

		/*  fatal(" !!! dma_addr = %08x, phys_addr = %08x\n",
		    (int)dma_addr, (int)phys_addr);  */

		res = memory_rw(cpu, cpu->mem, phys_addr,
		    &data[i], 1, writeflag, PHYSICAL | NO_EXCEPTIONS);

		dma_addr ++;
		i++;
	}


	return len;
}


/*
 *  dev_jazz_tick():
 */
void dev_jazz_tick(struct cpu *cpu, void *extra)
{
	struct jazz_data *d = extra;

	/*  Used by NetBSD/arc and OpenBSD/arc:  */
	if (d->interval_start > 0 && d->interval > 0
	    && (d->int_enable_mask & 2) /* Hm? */ ) {
		d->interval -= 2;
		if (d->interval <= 0) {
			debug("[ jazz: interval timer interrupt ]\n");
			cpu_interrupt(cpu, 8 + PICA_TIMER_IRQ);
		}
	}

	/*  Linux?  */
	if (d->jazz_timer_value != 0) {
		d->jazz_timer_current -= 5;
		if (d->jazz_timer_current < 1) {
			d->jazz_timer_current = d->jazz_timer_value;
			cpu_interrupt(cpu, 6);
		}
	}
}


/*
 *  dev_jazz_access():
 */
int dev_jazz_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct jazz_data *d = (struct jazz_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	switch (relative_addr) {
	case R4030_SYS_TL_BASE:
		if (writeflag == MEM_WRITE) {
			d->dma_translation_table_base = idata;
		} else {
			odata = d->dma_translation_table_base;
		}
		break;
	case R4030_SYS_TL_LIMIT:
		if (writeflag == MEM_WRITE) {
			d->dma_translation_table_limit = idata;
		} else {
			odata = d->dma_translation_table_limit;
		}
		break;
	case R4030_SYS_TL_IVALID:
		/*  TODO: Does invalidation actually need to be implemented?  */
		break;
	case R4030_SYS_DMA0_REGS:
		if (writeflag == MEM_WRITE) {
			d->dma0_mode = idata;
		} else {
			odata = d->dma0_mode;
		}
		break;
	case R4030_SYS_DMA0_REGS + 0x8:
		if (writeflag == MEM_WRITE) {
			d->dma0_enable = idata;
		} else {
			odata = d->dma0_enable;
		}
		break;
	case R4030_SYS_DMA0_REGS + 0x10:
		if (writeflag == MEM_WRITE) {
			d->dma0_count = idata;
		} else {
			odata = d->dma0_count;
		}
		break;
	case R4030_SYS_DMA0_REGS + 0x18:
		if (writeflag == MEM_WRITE) {
			d->dma0_addr = idata;
		} else {
			odata = d->dma0_addr;
		}
		break;
	case R4030_SYS_ISA_VECTOR:
		/*  ?  */
printf("R4030_SYS_ISA_VECTOR\n");
		{
			uint32_t x = d->isa_int_asserted
			    /* & d->int_enable_mask */;
			odata = 0;
			while (odata < 16) {
				if (x & (1 << odata))
					break;
				odata ++;
			}
			if (odata >= 16)
				odata = 0;
		}
		break;
	case R4030_SYS_IT_VALUE:  /*  Interval timer reload value  */
		if (writeflag == MEM_WRITE) {
			d->interval_start = idata;
			d->interval = d->interval_start;
		} else
			odata = d->interval_start;
		break;
	case R4030_SYS_IT_STAT:
		/*  Accessing this word seems to acknowledge interrupts?  */
		cpu_interrupt_ack(cpu, 8 + PICA_TIMER_IRQ);
		if (writeflag == MEM_WRITE)
			d->interval = idata;
		else
			odata = d->interval;
		d->interval = d->interval_start;
		break;
	case R4030_SYS_EXT_IMASK:
		if (writeflag == MEM_WRITE) {
			d->int_enable_mask = idata;
			/*  Do a "nonsense" interrupt recalibration:  */
			cpu_interrupt_ack(cpu, 8);
		} else
			odata = d->int_enable_mask;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ jazz: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ jazz: unimplemented read from address 0x%x"
			    " ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_jazz_access_a0():
 *
 *  ISA interrupt stuff, high 8 interrupts.
 */
int dev_jazz_access_a0(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct jazz_data *d = (struct jazz_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);
	odata = 0;

	switch (relative_addr) {
	case 0:
		if (writeflag == MEM_WRITE) {
			/*  TODO: only if idata == 0x20?  */
			d->isa_int_asserted &= 0xff;
			cpu_interrupt_ack(cpu, 8 + 0);
		}
		break;
	case 1:
		if (writeflag == MEM_WRITE) {
			idata = ((idata ^ 0xff) & 0xff) << 8;
			d->isa_int_enable_mask =
			    (d->isa_int_enable_mask & 0xff) | idata;
			debug("[ jazz_isa_a0: setting isa_int_enable_mask "
			    "to 0x%04x ]\n", (int)d->isa_int_enable_mask);
			/*  Recompute interrupt stuff:  */
			cpu_interrupt_ack(cpu, 8 + 0);
		} else
			odata = d->isa_int_enable_mask;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ jazz_isa_a0: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ jazz_isa_a0: unimplemented read from address 0x%x"
			    " ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_jazz_access_20():
 *
 *  ISA interrupt stuff, low 8 interrupts.
 */
int dev_jazz_access_20(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct jazz_data *d = (struct jazz_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);
	odata = 0;

	switch (relative_addr) {
	case 0:
		if (writeflag == MEM_WRITE) {
			/*  TODO: only if idata == 0x20?  */
			d->isa_int_asserted &= 0xff00;
			cpu_interrupt_ack(cpu, 8 + 0);
		}
		break;
	case 1:
		if (writeflag == MEM_WRITE) {
			idata = (idata ^ 0xff) & 0xff;
			d->isa_int_enable_mask =
			    (d->isa_int_enable_mask & 0xff00) | idata;
			debug("[ jazz_isa_20: setting isa_int_enable_mask "
			    "to 0x%04x ]\n", (int)d->isa_int_enable_mask);
			/*  Recompute interrupt stuff:  */
			cpu_interrupt_ack(cpu, 8 + 0);
		} else
			odata = d->isa_int_enable_mask;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ jazz_isa_20: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ jazz_isa_20: unimplemented read from address 0x%x"
			    " ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_jazz_access_jazzio():
 *
 *  See jazzio_intr() in NetBSD's
 *  /usr/src/sys/arch/arc/jazz/jazzio.c for more info.
 */
int dev_jazz_access_jazzio(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct jazz_data *d = (struct jazz_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i, v;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0:
		v = 0;
		for (i=0; i<15; i++) {
			if (d->int_asserted & (1<<i)) {
				v = i+1;
				break;
			}
		}
		odata = v << 2;
		break;
	case 2:
		/*  TODO: Should this be here?!  */

		if (writeflag == MEM_WRITE)
			d->jazz_timer_value = idata;
		else
			odata = d->jazz_timer_value;
		break;
	default:
		if (writeflag == MEM_WRITE) {
			fatal("[ jazzio: unimplemented write to address 0x%x"
			    ", data=0x%02x ]\n", (int)relative_addr, (int)idata);
		} else {
			fatal("[ jazzio: unimplemented read from address 0x%x"
			    " ]\n", (int)relative_addr);
		}
	}

	/*  This is needed by Windows NT during startup:  */
	cpu_interrupt_ack(cpu, 3);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_jazz_init():
 */
struct jazz_data *dev_jazz_init(struct cpu *cpu, struct memory *mem,
	uint64_t baseaddr)
{
	struct jazz_data *d = malloc(sizeof(struct jazz_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct jazz_data));

	d->cpu = cpu;

	d->isa_int_enable_mask = 0xffff;

	memory_device_register(mem, "jazz", baseaddr, DEV_JAZZ_LENGTH,
	    dev_jazz_access, (void *)d, MEM_DEFAULT, NULL);

	memory_device_register(mem, "jazz_isa_20", 0x90000020ULL, 2,
	    dev_jazz_access_20, (void *)d, MEM_DEFAULT, NULL);

	memory_device_register(mem, "jazz_isa_a0", 0x900000a0ULL, 2,
	    dev_jazz_access_a0, (void *)d, MEM_DEFAULT, NULL);

	memory_device_register(mem, "pica_jazzio", 0xf0000000ULL, 4,
	    dev_jazz_access_jazzio, (void *)d, MEM_DEFAULT, NULL);

	cpu_add_tickfunction(cpu, dev_jazz_tick, d, DEV_JAZZ_TICKSHIFT);

	return d;
}


/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_asc.c,v 1.2 2003-11-06 13:56:06 debug Exp $
 *
 *  SCSI controller for some DECsystems.
 *
 *  TODO :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "ncr53c9xreg.h"

struct asc_data {
	int		irq_nr;

	/*  Read registers and write registers:  */
	uint32_t	reg_ro[0x10];
	uint32_t	reg_wo[0x10];
};

/*  (READ/WRITE name, if split)  */
char *asc_reg_names[0x10] = {
	"NCR_TCL", "NCR_TCM", "NCR_FIFO", "NCR_CMD",
	"NCR_STAT/NCR_SELID", "NCR_INTR/NCR_TIMEOUT", "NCR_STEP/NCR_SYNCTP", "NCR_FFLAG/NCR_SYNCOFF",
	"NCR_CFG1", "NCR_CCF", "NCR_TEST", "NCR_CFG2",
	"NCR_CFG3", "reg_0xd", "NCR_TCH", "reg_0xf"
};


/*
 *  dev_asc_tick():
 *
 *  This function is called "every now and then" from the CPU
 *  main loop.
 */
void dev_asc_tick(struct cpu *cpu, void *extra)
{
	struct asc_data *d = extra;

	if (d->reg_ro[NCR_STAT] & NCRSTAT_INT)
		cpu_interrupt(cpu, d->irq_nr);
}


/*
 *  dev_asc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_asc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i, regnr;
	struct asc_data *d = extra;
	int idata = 0, odata=0, odata_set=0;

	dev_asc_tick(cpu, extra);

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	odata_set = 1;
	regnr = relative_addr / 4;

	if (writeflag==MEM_WRITE)
		d->reg_wo[regnr] = idata;
	else
		odata = d->reg_ro[regnr];

	/*  Some registers are read/write. Copy contents of reg_wo to reg_ro:  */
	d->reg_ro[ 0] = d->reg_wo[0];
	d->reg_ro[ 1] = d->reg_wo[1];
	d->reg_ro[ 2] = d->reg_wo[2];
	d->reg_ro[ 3] = d->reg_wo[3];
	d->reg_ro[ 8] = d->reg_wo[8];
	d->reg_ro[ 9] = d->reg_wo[9];
	d->reg_ro[10] = d->reg_wo[10];
	d->reg_ro[11] = d->reg_wo[11];
	d->reg_ro[12] = d->reg_wo[12];
	d->reg_ro[14] = d->reg_wo[14];

	if (writeflag==MEM_READ) {
		debug("[ asc: read from %s: 0x%02x", asc_reg_names[regnr], odata);
	} else {
		debug("[ asc: write to  %s: 0x%02x", asc_reg_names[regnr], idata);
	}

	if (regnr == NCR_CMD && writeflag==MEM_WRITE) {
		debug(" ");
		if (idata & NCRCMD_DMA)
			debug("[DMA] ");

		d->reg_ro[NCR_INTR] &= ~(NCRINTR_FC | NCRINTR_SBR | NCRINTR_BS);

		switch (idata & 0x7f) {
		case NCRCMD_NOP:
			debug("NOP");
			break;
		case NCRCMD_RSTCHIP:
			debug("RSTCHIP");
			/*  TODO:  actually reset the chip  */
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			break;
		case NCRCMD_RSTSCSI:
			debug("RSTSCSI");
			/*  TODO:  actually reset the bus  */
			d->reg_ro[NCR_INTR] |= NCRINTR_SBR;
/*			d->reg_ro[NCR_INTR] |= NCRINTR_BS; */
			d->reg_ro[NCR_INTR] |= NCRINTR_RESEL;
/*			d->reg_ro[NCR_INTR] |= NCRINTR_FC;  */
			break;
		case NCRCMD_SELNATN:
			debug("SELNATN: select without atn");
			/*  TODO  */
			d->reg_ro[NCR_INTR] |= NCRINTR_BS;
			d->reg_ro[NCR_INTR] |= NCRINTR_RESEL;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			break;
		default:
			debug("(unknown cmd)");
		}

		d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
	}

	if (regnr == NCR_INTR && writeflag==MEM_READ) {
		/*  TODO: this ack is just something I made up  */
		cpu_interrupt_ack(cpu, d->irq_nr);

		d->reg_ro[NCR_INTR] &= ~NCRINTR_SBR;
/*		d->reg_ro[NCR_INTR] &= ~NCRINTR_FC; */
	}

	if (regnr == NCR_CFG1) {
		/*  TODO: other bits  */
		debug(" parity %s,", d->reg_ro[regnr] & NCRCFG1_PARENB? "enabled" : "disabled");
		debug(" scsi_id %i", d->reg_ro[regnr] & 0x7);
	}

	debug(" ]\n");

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
	}

	return 1;
}


/*
 *  dev_asc_init():
 */
void dev_asc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct asc_data *d;

	d = malloc(sizeof(struct asc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct asc_data));
	d->irq_nr = irq_nr;

	memory_device_register(mem, "asc", baseaddr, DEV_ASC_LENGTH, dev_asc_access, d);
	cpu_add_tickfunction(cpu, dev_asc_tick, d, 10);  /*  every 1024:th cycle  */
}


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
 *  $Id: dev_asc.c,v 1.5 2003-11-09 04:13:39 debug Exp $
 *
 *  'asc' SCSI controller for some DECsystems.
 *
 *  Supposed to support SCSI-1 and SCSI-2. I've not yet found any docs
 *  on NCR53C9X, so I'll try to implement this device from LSI53CF92A docs
 *  instead.
 *
 *	NCR53C94 registers	at base + 0
 *	DMA address register	at base + 0x40000
 *	128K SRAM buffer	at base + 0x80000
 *	ROM			at base + 0xc0000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "devices.h"
#include "diskimage.h"

#include "ncr53c9xreg.h"

#define	ASC_FIFO_LEN		16
#define	STATE_DISCONNECTED	0
#define	STATE_INITIATOR		1
#define	STATE_TARGET		2

struct asc_data {
	int		irq_nr;

	int		cur_state;

	/*  FIFO:  */
	unsigned char	fifo[ASC_FIFO_LEN];
	int		fifo_in;
	int		fifo_out;
	int		n_bytes_in_fifo;		/*  cached  */

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
 *  dev_asc_fifo_flush():
 *
 *  Flush the fifo.
 */
void dev_asc_fifo_flush(struct asc_data *d)
{
	d->fifo[0] = 0x00;
	d->fifo_in = 0;
	d->fifo_out = 0;
	d->n_bytes_in_fifo = 0;
}


/*
 *  dev_asc_reset():
 *
 *  Reset the state of the asc.
 */
void dev_asc_reset(struct asc_data *d)
{
	d->cur_state = STATE_DISCONNECTED;
	dev_asc_fifo_flush(d);

	/*  According to table 4.1 in the LSI53CF92A manual:  */
	memset(d->reg_wo, 0, sizeof(d->reg_wo));
	d->reg_wo[NCR_TCH] = 0x94;
	d->reg_wo[NCR_CCF] = 2;
	memcpy(d->reg_ro, d->reg_wo, sizeof(d->reg_ro));
	d->reg_wo[NCR_SYNCTP] = 5;
}


/*
 *  dev_asc_fifo_read():
 *
 *  Read a byte from the asc FIFO.
 */
int dev_asc_fifo_read(struct asc_data *d)
{
	int res = d->fifo[d->fifo_out];
	d->fifo_out = (d->fifo_out + 1) % ASC_FIFO_LEN;
	d->n_bytes_in_fifo --;

	if (d->fifo_in == d->fifo_out)
		fatal("dev_asc: WARNING! FIFO overrun!\n");

	return res;
}


/*
 *  dev_asc_fifo_write():
 *
 *  Write a byte to the asc FIFO.
 */
void dev_asc_fifo_write(struct asc_data *d, unsigned char data)
{
	d->fifo[d->fifo_in] = data;
	d->fifo_in = (d->fifo_in + 1) % ASC_FIFO_LEN;
	d->n_bytes_in_fifo ++;

	if (d->fifo_in == d->fifo_out)
		fatal("dev_asc: WARNING! FIFO overrun on write!\n");
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
	int target_exists;
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

	odata_set = 1;  odata = 0;
	regnr = relative_addr / 4;


	d->reg_ro[NCR_FFLAG] = ((d->reg_ro[NCR_STEP] & 0x7) << 5) + d->n_bytes_in_fifo;

	if (regnr < 0x10) {
		if (writeflag==MEM_WRITE)
			d->reg_wo[regnr] = idata;
		else
			odata = d->reg_ro[regnr];

		if (writeflag==MEM_READ) {
			debug("[ asc: read from %s: 0x%02x", asc_reg_names[regnr], odata);
		} else {
			debug("[ asc: write to  %s: 0x%02x", asc_reg_names[regnr], idata);
		}
	} else {
		if (writeflag==MEM_READ) {
			debug("[ asc: read from 0x%04x: 0x%02x", regnr, odata);
		} else {
			debug("[ asc: write to  0x%04x: 0x%02x", regnr, idata);
		}
	}

	/*  Some registers are read/write. Copy contents of reg_wo to reg_ro:  */
	d->reg_ro[ 2] = d->reg_wo[2];
	d->reg_ro[ 3] = d->reg_wo[3];
	d->reg_ro[ 8] = d->reg_wo[8];
	d->reg_ro[ 9] = d->reg_wo[9];
	d->reg_ro[10] = d->reg_wo[10];
	d->reg_ro[11] = d->reg_wo[11];
	d->reg_ro[12] = d->reg_wo[12];

	if (regnr == NCR_FIFO) {
		if (writeflag == MEM_WRITE)
			dev_asc_fifo_write(d, idata);
		else
			odata = dev_asc_fifo_read(d);
	}

	if (regnr == NCR_CMD && writeflag==MEM_WRITE) {
		debug(" ");
		if (idata & NCRCMD_DMA)
			debug("[DMA] ");

		switch (idata & ~NCRCMD_DMA) {
		case NCRCMD_NOP:
			debug("NOP");
			break;
		case NCRCMD_FLUSH:
			debug("FLUSH");
			/*  Flush the FIFO:  */
			dev_asc_fifo_flush(d);
			break;
		case NCRCMD_RSTCHIP:
			debug("RSTCHIP");
			/*  Hardware reset.  */
			dev_asc_reset(d);
			break;
		case NCRCMD_RSTSCSI:
			debug("RSTSCSI");
			/*  No interrupt if interrupts are disabled.  */
			if (!(d->reg_wo[NCR_CFG1] & NCRCFG1_SRR))
				d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_SBR;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			d->cur_state = STATE_DISCONNECTED;
			break;
		case NCRCMD_ENSEL:
			debug("ENSEL");
			/*  TODO  */
			break;
		case NCRCMD_SETATN:
			debug("SETATN");
			/*  TODO: set ATN  */
			break;
		case NCRCMD_RSTATN:
			debug("RSTATN");
			/*  TODO: clear ATN  */
			break;
		case NCRCMD_SELATN:
		case NCRCMD_SELNATN:
			if ((idata & ~NCRCMD_DMA) == NCRCMD_SELATN)
				debug("SELATN: select with atn, id %i", d->reg_wo[NCR_SELID] & 7);
			else
				debug("SELNATN: select without atn, id %i", d->reg_wo[NCR_SELID] & 7);

			target_exists = diskimage_exist(d->reg_wo[NCR_SELID] & 7);

			if (target_exists) {
				/*  Selected:  */
				d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
				if ((idata & ~NCRCMD_DMA) == NCRCMD_SELATN)
					d->reg_ro[NCR_INTR] |= NCRINTR_SELATN;
				else
					d->reg_ro[NCR_INTR] |= NCRINTR_SEL;
				d->reg_ro[NCR_INTR] |= NCRINTR_FC;
				d->reg_ro[NCR_INTR] |= NCRINTR_BS;
				d->reg_ro[NCR_STEP] &= ~7;
				d->reg_ro[NCR_STEP] |= 4;
				d->cur_state = STATE_INITIATOR;
			} else {
				/*
				 *  Selection failed, non-existant scsi ID:
				 *
				 *  This is good enough to fool both Ultrix and
				 *  NetBSD to continue detection of other IDs,
				 *  without giving any warnings.
				 */
				d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
				d->reg_ro[NCR_INTR] |= NCRINTR_DIS;
				d->reg_ro[NCR_STEP] &= ~7;
				d->reg_ro[NCR_STEP] |= 0;
				dev_asc_fifo_flush(d);
				d->cur_state = STATE_DISCONNECTED;
			}
			break;
		case NCRCMD_TRPAD:
			debug("TRPAD");

			if (d->cur_state == STATE_DISCONNECTED) {
				fatal("[ dev_asc: TRPAD: bad state ]\n");
				break;
			}

			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			d->reg_ro[NCR_INTR] |= NCRINTR_BS;
			d->reg_ro[NCR_INTR] |= NCRINTR_FC;
			d->reg_ro[NCR_STAT] |= NCRSTAT_TC;
			d->reg_ro[NCR_TCL] = 0;
			dev_asc_fifo_flush(d);
			break;
		case NCRCMD_TRANS:
			debug("TRANS");

			if (d->cur_state == STATE_DISCONNECTED) {
				fatal("[ dev_asc: TRANS: bad state ]\n");
				break;
			}

			/*  TODO  */

			d->reg_ro[NCR_STAT] |= NCRSTAT_INT;
			dev_asc_fifo_flush(d);
			break;
		default:
			fatal("(unknown asc cmd 0x%02x)", idata);
			exit(1);
		}
	}

	if (regnr == NCR_INTR && writeflag==MEM_READ) {
		/*  Reading the interrupt register de-asserts the interrupt pin:  */
		cpu_interrupt_ack(cpu, d->irq_nr);

		if (d->reg_ro[NCR_STAT] & NCRSTAT_INT) {
			/*  INTR, STEP, and STAT are all cleared, according
				to page 64 of the LSI53CF92A manual.  */
			d->reg_ro[NCR_INTR] = 0;
			d->reg_ro[NCR_STEP] = 0;
			d->reg_ro[NCR_STAT] = 0;
		}
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
	cpu_add_tickfunction(cpu, dev_asc_tick, d, 12);  /*  every 4096:th cycle  */
}


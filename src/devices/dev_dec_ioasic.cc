/*
 *  Copyright (C) 2004-2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: IOASIC device used in some DECstation machines
 *
 *  TODO:  Lots of stuff, such as DMA and all bits in the control registers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "interrupt.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/dec_kn03.h"
#include "thirdparty/tc_ioasicreg.h"


#define IOASIC_DEBUG
/* #define debug fatal */


void dec_ioasic_reassert(struct dec_ioasic_data* d)
{
	// printf("[ intr = 0x%08x, imsk = 0x%08x ]\n", d->intr, d->imsk);

	if (d->intr & d->imsk && !d->int_asserted) {
		d->int_asserted = 1;
		d->irq->interrupt_assert(d->irq);
	}
	if (!(d->intr & d->imsk) && d->int_asserted) {
		d->int_asserted = 0;
		d->irq->interrupt_deassert(d->irq);
	}
}


DEVICE_ACCESS(dec_ioasic)
{
	struct dec_ioasic_data *d = (struct dec_ioasic_data *) extra;
	uint64_t idata = 0, odata = 0;
	// uint64_t curptr;
	// uint32_t csr;
	// int dma_len, dma_res, regnr;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	
	case 0:
		/*  NetBSD/pmax and OpenBSD/pmax read from this address.  */
		break;

	case IOASIC_SCSI_DMAPTR:
		if (writeflag == MEM_WRITE)
			d->scsi_dmaptr = idata;
		else
			odata = d->scsi_dmaptr;

		debug("[ dec_ioasic: %s SCSI_DMAPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCSI_NEXTPTR:
		if (writeflag == MEM_WRITE)
			d->scsi_nextptr = idata;
		else
			odata = d->scsi_nextptr;

		debug("[ dec_ioasic: %s SCSI_NEXTPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_LANCE_DMAPTR:
		if (writeflag == MEM_WRITE)
			d->lance_dmaptr = idata;
		else
			odata = d->lance_dmaptr;

		debug("[ dec_ioasic: %s LANCE_DMAPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_FLOPPY_DMAPTR:
		if (writeflag == MEM_WRITE)
			d->floppy_dmaptr = idata;
		else
			odata = d->floppy_dmaptr;

		debug("[ dec_ioasic: %s FLOPPY_DMAPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_X_DMAPTR:
		if (writeflag == MEM_WRITE)
			d->isdn_x_dmaptr = idata;
		else
			odata = d->isdn_x_dmaptr;

		debug("[ dec_ioasic: %s ISDN_X_DMAPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_X_NEXTPTR:
		if (writeflag == MEM_WRITE)
			d->isdn_x_nextptr = idata;
		else
			odata = d->isdn_x_nextptr;

		debug("[ dec_ioasic: %s ISDN_X_NEXTPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_R_DMAPTR:
		if (writeflag == MEM_WRITE)
			d->isdn_r_dmaptr = idata;
		else
			odata = d->isdn_r_dmaptr;

		debug("[ dec_ioasic: %s ISDN_R_DMAPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_R_NEXTPTR:
		if (writeflag == MEM_WRITE)
			d->isdn_r_nextptr = idata;
		else
			odata = d->isdn_r_nextptr;

		debug("[ dec_ioasic: %s ISDN_R_NEXTPTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_CSR:
		if (writeflag == MEM_WRITE)
			d->csr = idata;
		else
			odata = d->csr;

		debug("[ dec_ioasic: %s CSR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
#if 0
		if (writeflag == MEM_WRITE) {
			csr = d->reg[(IOASIC_CSR - IOASIC_SLOT_1_START) / 0x10];

			d->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] &=
			    ~IOASIC_INTR_T2_PAGE_END;

			if (csr & IOASIC_CSR_DMAEN_T2) {
				/*  Transmit data:  */
				curptr = (d->reg[(IOASIC_SCC_T2_DMAPTR -
				    IOASIC_SLOT_1_START) / 0x10] >> 3)
				    | ((d->reg[(IOASIC_SCC_T2_DMAPTR -
				    IOASIC_SLOT_1_START) / 0x10] & 0x1f) << 29);
				dma_len = 0x1000 - (curptr & 0xffc);

				if ((curptr & 0xfff) == 0)
					break;

				if (d->dma_func[3] != NULL) {
					d->dma_func[3](cpu,
					    d->dma_func_extra[3], curptr,
					    dma_len, 1);
				} else
					fatal("[ dec_ioasic: DMA tx: data @ "
					    "%08x, len %i bytes, but no "
					    "handler? ]\n", (int)curptr,
					    dma_len);

				/*  and signal the end of page:  */
				d->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) /
				    0x10] |= IOASIC_INTR_T2_PAGE_END;

				d->reg[(IOASIC_CSR - IOASIC_SLOT_1_START) /
				    0x10] &= ~IOASIC_CSR_DMAEN_T2;
				curptr |= 0xfff;
				curptr ++;

				d->reg[(IOASIC_SCC_T2_DMAPTR -
				    IOASIC_SLOT_1_START) / 0x10] = ((curptr <<
				    3) & ~0x1f) | ((curptr >> 29) & 0x1f);
			}

			if (csr & IOASIC_CSR_DMAEN_R2) {
				/*  Receive data:  */
				curptr = (d->reg[(IOASIC_SCC_R2_DMAPTR -
				    IOASIC_SLOT_1_START) / 0x10] >> 3)
				    | ((d->reg[(IOASIC_SCC_R2_DMAPTR -
				    IOASIC_SLOT_1_START) / 0x10] & 0x1f) << 29);
				dma_len = 0x1000 - (curptr & 0xffc);

				dma_res = 0;
				if (d->dma_func[3] != NULL) {
					dma_res = d->dma_func[3](cpu,
					    d->dma_func_extra[3], curptr,
					    dma_len, 0);
				} else
					fatal("[ dec_ioasic: DMA tx: data @ "
					    "%08x, len %i bytes, but no "
					    "handler? ]\n", (int)curptr,
					    dma_len);

				/*  and signal the end of page:  */
				if (dma_res > 0) {
					if ((curptr & 0x800) != ((curptr +
					    dma_res) & 0x800))
						d->reg[(IOASIC_INTR -
						    IOASIC_SLOT_1_START) / 0x10]
						    |= IOASIC_INTR_R2_HALF_PAGE;
					curptr += dma_res;
/*					d->reg[(IOASIC_CSR - IOASIC_SLOT_1_START
					   ) / 0x10] &= ~IOASIC_CSR_DMAEN_R2; */
					d->reg[(IOASIC_SCC_R2_DMAPTR -
					    IOASIC_SLOT_1_START) / 0x10] =
					    ((curptr << 3) & ~0x1f) | ((curptr
					    >> 29) & 0x1f);
				}
			}
		}
#endif
		break;

	case IOASIC_INTR:
		if (writeflag == MEM_WRITE) {
			/*  Clear bits on write?  */
			d->intr &= ~idata;
			dec_ioasic_reassert(d);
		} else {
			odata = d->intr;

			/*  Note/TODO: How about other models than KN03?  */
			//if (!d->rackmount_flag)
			//	odata |= KN03_INTR_PROD_JUMPER;
		}

		debug("[ dec_ioasic: %s INTR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);

		break;

	case IOASIC_IMSK:
		if (writeflag == MEM_WRITE) {
			d->imsk = idata;
			dec_ioasic_reassert(d);
		} else {
			odata = d->imsk;
		}

		debug("[ dec_ioasic: %s IMSK, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_X_DATA:
		if (writeflag == MEM_WRITE)
			d->isdn_x_data = idata;
		else
			odata = d->isdn_x_data;

		debug("[ dec_ioasic: %s ISDN_X_DATA, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_ISDN_R_DATA:
		if (writeflag == MEM_WRITE)
			d->isdn_r_data = idata;
		else
			odata = d->isdn_r_data;

		debug("[ dec_ioasic: %s ISDN_R_DATA, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_LANCE_DECODE:
		if (writeflag == MEM_WRITE)
			d->lance_decode = idata;
		else
			odata = d->lance_decode;

		debug("[ dec_ioasic: %s LANCE_DECODE, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCSI_DECODE:
		if (writeflag == MEM_WRITE)
			d->scsi_decode = idata;
		else
			odata = d->scsi_decode;

		debug("[ dec_ioasic: %s SCSI_DECODE, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCC0_DECODE:
		if (writeflag == MEM_WRITE)
			d->scc0_decode = idata;
		else
			odata = d->scc0_decode;

		debug("[ dec_ioasic: %s SCC0_DECODE, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCC1_DECODE:
		if (writeflag == MEM_WRITE)
			d->scc1_decode = idata;
		else
			odata = d->scc1_decode;

		debug("[ dec_ioasic: %s SCC1_DECODE, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_FLOPPY_DECODE:
		if (writeflag == MEM_WRITE)
			d->floppy_decode = idata;
		else
			odata = d->floppy_decode;

		debug("[ dec_ioasic: %s FLOPPY_DECODE, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCSI_SCR:
		if (writeflag == MEM_WRITE)
			d->scsi_scr = idata;
		else
			odata = d->scsi_scr;

		debug("[ dec_ioasic: %s SCSI_SCR, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCSI_SDR0:
		if (writeflag == MEM_WRITE)
			d->scsi_sdr0 = idata;
		else
			odata = d->scsi_sdr0;

		debug("[ dec_ioasic: %s SCSI_SDR0, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;

	case IOASIC_SCSI_SDR1:
		if (writeflag == MEM_WRITE)
			d->scsi_sdr1 = idata;
		else
			odata = d->scsi_sdr1;

		debug("[ dec_ioasic: %s SCSI_SDR1, data=0x%08x ]\n",
			writeflag == MEM_WRITE ? "write to" : "read from",
			writeflag == MEM_WRITE ? idata : odata);
		break;


	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x00:
	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x04:
	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x08:
	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x0c:
	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x10:
	case IOASIC_SYS_ETHER_ADDRESS(0) + 0x14:
		/*  Station's ethernet address:  */
		if (writeflag == MEM_WRITE) {
			fatal("[ dec_ioasic: attempt to write to the station's"
			    " ethernet address. ignored for now. ]\n");
		} else {
			odata = ((relative_addr - IOASIC_SYS_ETHER_ADDRESS(0)) / 4 + 1) * 0x10;
		}
		break;

	/*  The DECstation 5000/125's PROM uses these for cache testing. TODO.  */
	case 0x0f004:
	case 0x1f008:
	case 0x2f00c:
	case 0x3f010:
	case 0x4f014:
	case 0x5f018:
	case 0x6f01c:
	case 0x7f020:
	case 0x8f024:
	case 0x9f028:
	case 0xaf02c:
	case 0xbf030:
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ dec_ioasic: unimplemented write to address "
			    "0x%llx, data=0x%08llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ dec_ioasic: unimplemented read from address "
			    "0x%llx ]\n", (long long)relative_addr);
		// exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec_ioasic_init():
 *
 *  For DECstation "type 4", the rackmount_flag selects which model type
 *  the IOASIC should identify itself as (5000 for zero, 5900 if rackmount_flag
 *  is non-zero). It is probably not meaningful on other machines than
 *  type 4.
 */
struct dec_ioasic_data *dev_dec_ioasic_init(struct cpu *cpu,
	struct memory *mem, uint64_t baseaddr, int rackmount_flag, struct interrupt* irqp)
{
	struct dec_ioasic_data *d;

	CHECK_ALLOCATION(d = (struct dec_ioasic_data *) malloc(sizeof(struct dec_ioasic_data)));
	memset(d, 0, sizeof(struct dec_ioasic_data));

	d->rackmount_flag = rackmount_flag;
	d->irq = irqp;

	memory_device_register(mem, "dec_ioasic", baseaddr,
	    DEV_DEC_IOASIC_LENGTH, dev_dec_ioasic_access, (void *)d,
	    DM_DEFAULT, NULL);

	return d;
}


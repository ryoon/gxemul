/*
 *  Copyright (C) 2004-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_wdsc.c,v 1.31 2006-03-04 12:38:49 debug Exp $
 *  
 *  WDSC SCSI (WD33C93) controller.
 *  (For SGI-IP22. See sys/arch/sgimips/hpc/sbic* in NetBSD for details.)
 *
 *  TODO:  This device doesn't do much yet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "wdsc_sbicreg.h"


struct wdsc_data {
	int		irq_nr;
	int		controller_nr;

	int		register_select;
	unsigned char	reg[DEV_WDSC_NREGS];

	int		irq_pending;

	int		buf_allocatedlen;
	int		buf_curptr;
	unsigned char	*buf;

	int		current_phase;

	struct scsi_transfer *xfer;
};


/*
 *  dev_wdsc_tick():
 */
void dev_wdsc_tick(struct cpu *cpu, void *extra)
{
	struct wdsc_data *d = extra;

	if (d->irq_pending)
		cpu_interrupt(cpu, d->irq_nr);
	else
		cpu_interrupt_ack(cpu, d->irq_nr);
}


/*
 *  dev_wdsc_regwrite():
 *
 *  Handle writes to WDSC registers.
 */
static void dev_wdsc_regwrite(struct cpu *cpu, struct wdsc_data *d, int idata)
{
	d->reg[d->register_select] = idata & 0xff;

	debug("[ wdsc: write to register %i", d->register_select);

	switch (d->register_select) {

	case SBIC_myid:
		debug(" (myid): 0x%02x => ", (int)idata);
		if (idata & SBIC_ID_FS_16_20)
			debug("16-20MHz, ");
		if (idata & SBIC_ID_FS_12_15)
			debug("12-15MHz, ");
		if (idata & SBIC_ID_RAF)
			debug("RAF(?), ");
		if (idata & SBIC_ID_EHP)
			debug("Parity, ");
		if (idata & SBIC_ID_EAF)
			debug("EnableAdvancedFeatures, ");
		debug("ID=%i", idata & SBIC_ID_MASK);
		break;

	case SBIC_control:
		debug(" (control): 0x%02x =>", (int)idata);
		if (idata & SBIC_CTL_DMA)
			debug(" SingleByteDMA");
		if (idata & SBIC_CTL_DBA_DMA)
			debug(" DirectBufferAccess");
		if (idata & SBIC_CTL_BURST_DMA)
			debug(" BurstDMA");
		if (idata & SBIC_CTL_HHP)
			debug(" HaltOnParity");
		if (idata & SBIC_CTL_EDI)
			debug(" EndDisconIntr");
		if (idata & SBIC_CTL_IDI)
			debug(" IntermediateDisconIntr");
		if (idata & SBIC_CTL_HA)
			debug(" HaltOnATN");
		if (idata & SBIC_CTL_HSP)
			debug(" HaltOnParityError");

		if (idata == SBIC_CTL_NO_DMA)
			debug(" PIO");

		/*  TODO:  When/how are interrupts acknowledged?  */
		if (idata & SBIC_CTL_EDI)
			d->irq_pending = 0;

		break;

	case SBIC_count_hi:
		debug(" (count_hi): 0x%02x", (int)idata);
		break;

	case SBIC_count_med:
		debug(" (count_med): 0x%02x", (int)idata);
		break;

	case SBIC_count_lo:
		debug(" (count_lo): 0x%02x", (int)idata);
		break;

	case SBIC_selid:
		debug(" (selid): 0x%02x => ", (int)idata);

		if (idata & SBIC_SID_SCC)
			debug("SelectCommandChaining, ");
		if (idata & SBIC_SID_FROM_SCSI)
			debug("FromSCSI, ");
		else
			debug("ToSCSI, ");

		debug("id %i", idata & SBIC_SID_IDMASK);
		break;

	case SBIC_rselid:
		debug(" (rselid): 0x%02x => ", (int)idata);

		if (idata & SBIC_RID_ER)
			debug("EnableReselection, ");
		if (idata & SBIC_RID_ES)
			debug("EnableSelection, ");
		if (idata & SBIC_RID_DSP)
			debug("DisableSelectParity, ");
		if (idata & SBIC_RID_SIV)
			debug("SourceIDValid, ");

		debug("id %i", idata & SBIC_RID_MASK);
		break;

	case SBIC_cmd:
		debug(" (cmd): 0x%02x => ", (int)idata);

		/*  SBT = Single Byte Transfer  */
		if (idata & SBIC_CMD_SBT)
			debug("SBT, ");

		/*  Handle commands:  */
		switch (idata & SBIC_CMD_MASK) {
		case SBIC_CMD_RESET:
			debug("RESET");
			d->irq_pending = 1;
			d->reg[SBIC_csr] = SBIC_CSR_RESET;
			break;
		case SBIC_CMD_ABORT:
			debug("ABORT");
			break;
		case SBIC_CMD_SEL_ATN:
			debug("SEL_ATN");
			d->irq_pending = 1;
			d->reg[SBIC_csr] = SBIC_CSR_SEL_TIMEO;
			if (d->controller_nr == 0 && diskimage_exist(
			    cpu->machine, d->reg[SBIC_selid] &
			    SBIC_SID_IDMASK, DISKIMAGE_SCSI)) {
				if (d->xfer != NULL)
					scsi_transfer_free(d->xfer);
				d->xfer = scsi_transfer_alloc();

				/*  According to NetBSD, we can go either to
				    SBIC_CSR_MIS_2 | CMD_PHASE, or
				    SBIC_CSR_MIS_2 | MESG_OUT_PHASE.  */

				d->reg[SBIC_csr] = SBIC_CSR_MIS_2 | CMD_PHASE;

				d->current_phase = CMD_PHASE;
			}
			break;
		case SBIC_CMD_XFER_INFO:
			debug("XFER_INFO");

			if (d->buf != NULL)
				free(d->buf);

			d->buf_allocatedlen = (d->reg[SBIC_count_hi] << 16)
			    + (d->reg[SBIC_count_med] << 8) +
			    d->reg[SBIC_count_lo];

			d->buf = malloc(d->buf_allocatedlen);
			if (d->buf == NULL) {
				fprintf(stderr, "out of memory in wdsc\n");
				exit(1);
			}

			d->buf_curptr = 0;
			d->irq_pending = 0;
			break;
		default:
			debug("unimplemented command");
		}
		break;

	case SBIC_data:
		debug(" (data): 0x%02x", (int)idata);

		switch (d->reg[SBIC_cmd] & ~SBIC_CMD_SBT) {
		case SBIC_CMD_XFER_INFO:
			if (d->buf == NULL || d->buf_curptr >=
			    d->buf_allocatedlen) {
				fprintf(stderr, "fatal error in wdsc\n");
				exit(1);
			}

			d->buf[d->buf_curptr++] = idata;

			if (d->buf_curptr >= d->buf_allocatedlen) {
				int res;

				/*
				 *  Transfer buf to/from the SCSI unit:
				 */

				switch (d->current_phase) {
				case CMD_PHASE:
					scsi_transfer_allocbuf(
					    &d->xfer->cmd_len, &d->xfer->cmd,
					    d->buf_allocatedlen, 1);
					memcpy(d->xfer->cmd, d->buf,
					    d->buf_allocatedlen);
					break;
				default:
					fatal("wdsc: unimplemented phase %i!\n",
					    d->current_phase);
				}

				res = diskimage_scsicommand(cpu,
				    d->reg[SBIC_selid] & SBIC_SID_IDMASK,
				    DISKIMAGE_SCSI, d->xfer);
				debug("{ res = %i }", res);

				d->irq_pending = 1;

				if (res == 2)
					d->reg[SBIC_csr] = SBIC_CSR_XFERRED |
					    DATA_OUT_PHASE;
				else
					d->reg[SBIC_csr] = SBIC_CSR_XFERRED |
					    DATA_IN_PHASE;

				/*  status phase?  msg in and msg out?  */
			}
			break;
		default:
			fatal("[ wdsc: don't know how to handle data for "
			    "cmd = 0x%02x ]\n", d->reg[SBIC_cmd]);
		}

		break;

	default:
		debug(" (TODO): 0x%02x", (int)idata);
	}

	debug(" ]\n");

	/*  After writing to a register, advance to the next:  */
	d->register_select ++;
}


/*
 *  dev_wdsc_access():
 */
DEVICE_ACCESS(wdsc)
{
	size_t i;
	struct wdsc_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*
	 *  All registers on the WDSC seem to be accessed by writing the
	 *  register number to one address (SBIC_ADDR), and then reading/
	 *  writing another address (SBIC_VAL).
	 *
	 *  On SGI-IP22, these are at offset 3 and 7, respectively.
	 *
	 *  TODO: If this device is to be used by other machine types, then
	 *        this needs to be conditionalized.
	 */
	relative_addr = (relative_addr - 3) / 4;

	switch (relative_addr) {

	case SBIC_ADDR:
		/*
		 *  Reading the combined ADDR/ASR returns the Status
		 *  Register, writing selects which register to access
		 *  via SBIC_VAL.
		 */
		if (writeflag == MEM_READ) {
			odata = SBIC_ASR_DBR;
			if (d->irq_pending)
				odata |= SBIC_ASR_INT;

			debug("[ wdsc: read from Status Register: %02x ]\n",
			    (int)odata);
		} else {
			d->register_select = idata & (DEV_WDSC_NREGS - 1);
		}
		break;

	case SBIC_VAL:
		if (writeflag == MEM_READ) {
			odata = d->reg[d->register_select];

			if (d->register_select == SBIC_csr) {
				/*  TODO: when should interrupts actually be
				    ack:ed?  */
				d->irq_pending = 0;
			}

			debug("[ wdsc: read from register %i: 0x%02x ]\n",
			    d->register_select, (int)odata);
		} else {
			dev_wdsc_regwrite(cpu, d, idata & 0xff);
		}
		break;

	default:
		/*  These should never occur:  */
		if (writeflag==MEM_READ) {
			fatal("[ wdsc: read from 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" (len=%i) ]\n", len);
		} else {
			fatal("[ wdsc: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				fatal(" %02x", data[i]);
			fatal(" (len=%i) ]\n", len);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_wdsc_tick(cpu, extra);

	return 1;
}


/*
 *  dev_wdsc_init():
 */
void dev_wdsc_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int controller_nr, int irq_nr)
{
	struct wdsc_data *d;

	d = malloc(sizeof(struct wdsc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct wdsc_data));
	d->irq_nr = irq_nr;
	d->controller_nr = controller_nr;

	memory_device_register(mem, "wdsc", baseaddr, DEV_WDSC_LENGTH,
	    dev_wdsc_access, d, DM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_wdsc_tick, d, 14, 0.0);
}


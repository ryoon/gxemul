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
 *  $Id: dev_wdsc.c,v 1.4 2004-06-12 11:47:38 debug Exp $
 *  
 *  WDSC SCSI (WD33C93) controller.
 *  (For SGI-IP22. See sys/arch/sgimips/hpc/sbic* in NetBSD for details.)
 *
 *  TODO:  This is just a dummy device so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "wdsc_sbicreg.h"


struct wdsc_data {
	int		register_select;

	unsigned char	reg[DEV_WDSC_NREGS];
};


/*
 *  dev_wdsc_regwrite():
 *
 *  Handle writes to WDSC registers.
 */
void dev_wdsc_regwrite(struct wdsc_data *d, int idata)
{
	d->reg[d->register_select] = idata & 0xff;

	debug("[ wdsc: write to register 0x%02x", d->register_select);

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

	case SBIC_cmd:
		debug(" (cmd): 0x%02x => ", (int)idata);

		/*  SBT = Single Byte Transfer  */
		if (idata & SBIC_CMD_SBT)
			debug("SBT, ");

		/*  Handle commands:  */
		switch (idata & SBIC_CMD_MASK) {
		case SBIC_CMD_RESET:
			debug("RESET");
			break;
		case SBIC_CMD_ABORT:
			debug("ABORT");
			break;
		default:
			debug("unimplemented command");
		}
		break;

	default:
		debug(" (not yet implemented): %02x", (int)idata);
	}

	debug(" ]\n");
}


/*
 *  dev_wdsc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_wdsc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	int i;
	struct wdsc_data *d = extra;
	uint64_t idata = 0, odata = 0;

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
			debug("[ wdsc: read from Status Register: %02x ]\n",
			    (int)odata);
		} else {
			d->register_select = idata & (DEV_WDSC_NREGS - 1);
		}
		break;

	case SBIC_VAL:
		if (writeflag == MEM_READ) {
			odata = d->reg[d->register_select];
			debug("[ wdsc: read from register 0x%02x: %02x ]\n",
			    d->register_select, (int)odata);
		} else {
			dev_wdsc_regwrite(d, idata & 0xff);
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

	return 1;
}


/*
 *  dev_wdsc_init():
 */
void dev_wdsc_init(struct memory *mem, uint64_t baseaddr)
{
	struct wdsc_data *d;

	d = malloc(sizeof(struct wdsc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct wdsc_data));

	memory_device_register(mem, "wdsc", baseaddr, DEV_WDSC_LENGTH,
	    dev_wdsc_access, d);
}


/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_adb.c,v 1.3 2005-11-29 09:32:58 debug Exp $
 *
 *  ADB (Apple peripherals) controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "adb_viareg.h"


#define debug fatal

#define	TICK_SHIFT		17
#define	DEV_ADB_LENGTH		0x2000

#define	N_ADB_REGS		0x20
#define	MAX_CMD			50

struct adb_data {
	int		irqnr;
	uint8_t		reg[N_ADB_REGS];

	int		cur_cmd_offset;
	uint8_t		cmd_buf[MAX_CMD];
};

#define	BUFB_nINTR		0x08
#define	BUFB_ACK		0x10
#define	BUFB_nTIP		0x20
#define	IFR_SR			0x04
#define	IFR_ANY			0x80
#define	ACR_SR_OUT		0x10



/*
 *  dev_adb_tick():
 */
void dev_adb_tick(struct cpu *cpu, void *extra)
{
	/*  struct adb_data *d = extra;  */

	/*  TODO  */
}


/*
 *  dev_adb_access():
 */
int dev_adb_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct adb_data *d = extra;
	uint8_t old = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ)
		odata = d->reg[relative_addr >> 8];
	else {
		old = d->reg[relative_addr >> 8];
		switch (relative_addr) {
		case vIFR:
			/*  Write 1s to clear:  */
			d->reg[relative_addr >> 8] &= ~idata;
			break;
		default:
			d->reg[relative_addr >> 8] = idata;
		}
	}



/*  TODO  */

odata = random();


	switch (relative_addr) {

	case vBufB:	/*  Register B  */
		if (writeflag == MEM_WRITE) {
			if (!(idata & BUFB_nTIP) &&
			    old & BUFB_nTIP) {
				/*  Begin transfer.  */
				d->reg[vIFR >> 8] |= IFR_ANY;
				d->reg[vIFR >> 8] |= IFR_SR;
				d->reg[vBufB >> 8] |= BUFB_nINTR;
				if (d->reg[vACR] & ACR_SR_OUT) {
					fatal("[ adb: BEGIN TX 0x%02x ]\n",
					    d->reg[vSR >> 8]);
					d->cur_cmd_offset = 0;
					d->cmd_buf[d->cur_cmd_offset++] =
					    d->reg[vSR >> 8];
				} else {
					fatal("[ adb: BEGIN RX ]\n");
				}
			} else if (!(idata & BUFB_nTIP) &&
			    (idata & BUFB_ACK) !=
			    (old & BUFB_ACK)) {
				/*  Ack.  */

				if (d->reg[vACR] & ACR_SR_OUT) {
					d->reg[vIFR >> 8] |= IFR_ANY;
					d->reg[vIFR >> 8] |= IFR_SR;
					d->reg[vBufB >> 8] |= BUFB_nINTR;
					fatal("[ adb: TX 0x%02x ]\n",
					    d->reg[vSR >> 8]);
					d->cmd_buf[d->cur_cmd_offset++] =
					    d->reg[vSR >> 8];
					if (d->cmd_buf[0] == 0x00 &&
					    d->cur_cmd_offset == 2) {
						/*  Command done.  */
						d->reg[vBufB >> 8] |= BUFB_nINTR;
						d->reg[vSR >> 8] = 0;
					}
				} else {
					d->reg[vIFR >> 8] |= IFR_ANY;
					d->reg[vIFR >> 8] |= IFR_SR;
					d->reg[vBufB >> 8] |= BUFB_nINTR;
					d->reg[vSR >> 8] = 0;
					fatal("[ adb: RX 0x%02x ]\n",
					    d->reg[vSR >> 8]);
				}
			}
		}
		break;

	case vDirB:	/*  Data direction  */
		break;

	case vSR:	/*  Shift register  */
		if (writeflag == MEM_WRITE) {
			fatal("[ adb: SR = 0x%02x ]\n", (int)idata);
		} else {
			d->reg[vIFR >> 8] &= ~IFR_SR;
		}
		break;

	case vACR:	/*  Aux control register  */
		break;

	case vIFR:	/*  Interrupt flag  */
		break;

	case vIER:	/*  Interrupt enable  */
		break;

	default:if (writeflag == MEM_READ)
			fatal("[ adb: READ from UNIMPLEMENTED 0x%x ]\n",
			    (int)relative_addr);
		else
			fatal("[ adb: WRITE to UNIMPLEMENTED 0x%x: 0x%x ]\n",
			    (int)relative_addr, (int)idata);
exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_adb():
 */
int devinit_adb(struct devinit *devinit)
{
	struct adb_data *d = malloc(sizeof(struct adb_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct adb_data));
	d->irqnr = devinit->irq_nr;

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_ADB_LENGTH, dev_adb_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(devinit->machine, dev_adb_tick, d, TICK_SHIFT);

	return 1;
}


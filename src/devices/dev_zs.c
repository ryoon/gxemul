/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_zs.c,v 1.26 2005-12-02 01:46:30 debug Exp $
 *  
 *  Zilog serial controller.
 *
 *  Work in progress...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "z8530reg.h"


#define	ZS_TICK_SHIFT		14
#define	ZS_N_REGS		16

#define debug fatal

struct zs_data {
	int		irq_nr;
	int		irq_asserted;
	int		addrmult;

	/*  2 of everything, because there are two channels.  */
	int		in_use[2];
	int		console_handle[2];
	int		reg_select[2];
	uint8_t		rr[2][ZS_N_REGS];
	uint8_t		wr[2][ZS_N_REGS];
};


/*
 *  dev_zs_tick():
 */
static void check_incoming(struct cpu *cpu, struct zs_data *d)
{
	d->rr[0][0] &= ~ZSRR0_RX_READY;
	d->rr[1][0] &= ~ZSRR0_RX_READY;
	if (d->in_use[0] && console_charavail(d->console_handle[0])) {
		d->rr[0][0] |= ZSRR0_RX_READY;
		d->rr[1][3] |= ZSRR3_IP_B_RX;
	}
	if (d->in_use[1] && console_charavail(d->console_handle[1])) {
		d->rr[1][0] |= ZSRR0_RX_READY;
		d->rr[1][3] |= ZSRR3_IP_A_RX;
	}
}


/*
 *  dev_zs_tick():
 */
void dev_zs_tick(struct cpu *cpu, void *extra)
{
	struct zs_data *d = (struct zs_data *) extra;
	int asserted = 0;

	check_incoming(cpu, d);

	if (d->rr[1][3] & ZSRR3_IP_B_RX && (d->wr[0][1]&0x18) != ZSWR1_RIE_NONE)
		asserted = 1;
	if (d->rr[1][3] & ZSRR3_IP_A_RX && (d->wr[1][1]&0x18) != ZSWR1_RIE_NONE)
		asserted = 1;

	if (d->rr[1][3] & ZSRR3_IP_B_TX && d->wr[0][1] & ZSWR1_TIE)
		asserted = 1;
	if (d->rr[1][3] & ZSRR3_IP_A_TX && d->wr[1][1] & ZSWR1_TIE)
		asserted = 1;

	if (!(d->wr[1][9] & ZSWR9_MASTER_IE))
		asserted = 0;

	if (asserted)
		cpu_interrupt(cpu, d->irq_nr);

	if (d->irq_asserted && !asserted)
		cpu_interrupt_ack(cpu, d->irq_nr);

	d->irq_asserted = asserted;
}


/*
 *  dev_zs_access():
 */
int dev_zs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr,
	unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct zs_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int port_nr;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*  Both ports are always ready to transmit:  */
	d->rr[0][0] |= ZSRR0_TX_READY | ZSRR0_DCD | ZSRR0_CTS;
	d->rr[1][0] |= ZSRR0_TX_READY | ZSRR0_DCD | ZSRR0_CTS;

	/*  Any available key strokes?  */
	check_incoming(cpu, d);

	relative_addr /= d->addrmult;

	port_nr = relative_addr / 2;
	relative_addr &= 1;

	switch (relative_addr) {

	case 0:	if (writeflag == MEM_READ) {
			odata = d->rr[port_nr][d->reg_select[port_nr]];
			if (d->reg_select[port_nr] != 0)
				debug("[ zs: read from port %i reg %2i: 0x%02x"
				    " ]\n", port_nr, d->reg_select[port_nr],
				    (int)odata);
			/*  Ack interrupt status:  */
			if (port_nr == 1 && d->reg_select[port_nr] == 3)
				d->rr[port_nr][d->reg_select[port_nr]] = 0;
			d->reg_select[port_nr] = 0;
		} else {
			if (d->reg_select[port_nr] == 0) {
				d->reg_select[port_nr] = idata & 15;
			} else {
				switch (d->reg_select[port_nr]) {
				default:debug("[ zs: write to UNIMPLEMENTED "
					    "port %i reg %2i: 0x%02x ]\n",
					    port_nr, d->reg_select[port_nr],
					    (int)idata);
				}
				d->reg_select[port_nr] = 0;
			}
		}
		break;

	case 1:	if (writeflag == MEM_READ) {
			if (console_charavail(d->console_handle[port_nr]))
				odata = console_readchar(d->
				    console_handle[port_nr]);
			else
				odata = 0;
		} else {
			console_putchar(d->console_handle[port_nr], idata&255);
			d->in_use[port_nr] = 1;
			if (port_nr == 0)
				d->rr[1][3] |= ZSRR3_IP_B_TX;
			else
				d->rr[1][3] |= ZSRR3_IP_A_TX;
		}
		break;
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_zs_tick(cpu, extra);

	return 1;
}


/*
 *  dev_zs_init():
 */
int dev_zs_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int addrmult, char *name_a,
	char *name_b)
{
	struct zs_data *d;

	d = malloc(sizeof(struct zs_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct zs_data));
	d->irq_nr   = irq_nr;
	d->addrmult = addrmult;

	d->console_handle[0] = console_start_slave(machine, name_a);
	d->console_handle[1] = console_start_slave(machine, name_b);

	memory_device_register(mem, "zs", baseaddr, DEV_ZS_LENGTH * addrmult,
	    dev_zs_access, d, DM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_zs_tick, d, ZS_TICK_SHIFT);

	return d->console_handle[0];
}


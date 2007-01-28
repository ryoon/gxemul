/*
 *  Copyright (C) 2004-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_au1x00.c,v 1.22 2007-01-28 14:40:54 debug Exp $
 *  
 *  Au1x00 (eg Au1500) pseudo device. See aureg.h for bitfield details.
 *
 *  Used in at least the MeshCube (Au1500) and on PB1000 (evbmips) boards.
 *
 *  This is basically just a huge TODO. :-)
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

#include "aureg.h"


struct au1x00_ic_data {
	int		ic_nr;
	uint32_t	request0_int;
	uint32_t	request1_int;
	uint32_t	config0;
	uint32_t	config1;
	uint32_t	config2;
	uint32_t	source;
	uint32_t	assign_request;
	uint32_t	wakeup;
	uint32_t	mask;
};

struct au1x00_uart_data {
	int		console_handle;
	int		uart_nr;
	int		irq_nr;
	int		in_use;
	uint32_t	int_enable;
	uint32_t	modem_control;
};


struct au1x00_pc_data {
	uint32_t	reg[PC_SIZE/4 + 2];
	int		irq_nr;
};


#if 0
/*  TODO: Convert this to the new interrupt syntax  */
/*
 *  Au1x00 interrupt routine:
 *
 *  TODO: This is just bogus so far.  For more info, read this:
 *  http://www.meshcube.org/cgi-bin/viewcvs.cgi/kernel/linux/arch/
 *	mips/au1000/common/
 *
 *  CPU int 2 = IC 0, request 0
 *  CPU int 3 = IC 0, request 1
 *  CPU int 4 = IC 1, request 0
 *  CPU int 5 = IC 1, request 1
 *
 *  Interrupts 0..31 are on interrupt controller 0, interrupts 32..63 are
 *  on controller 1.
 *
 *  Special case: if irq_nr == 64+8, then this just updates the CPU
 *  interrupt assertions.
 */
void au1x00_interrupt(struct machine *m, struct cpu *cpu,
	int irq_nr, int assrt)
{
	uint32_t ms;

	irq_nr -= 8;
	debug("au1x00_interrupt(): irq_nr=%i assrt=%i\n", irq_nr, assrt);

	if (irq_nr < 64) {
		ms = 1 << (irq_nr & 31);

fatal("TODO: legacy interrupt rewrite!\n");
abort();

//		if (assrt)
//			m->md_int.au1x00_ic_data->request0_int |= ms;
//		else
//			m->md_int.au1x00_ic_data->request0_int &= ~ms;

		/*  TODO: Controller 1  */
	}

fatal("TODO: legacy interrupt rewrite!\n");
abort();

//	if ((m->md_int.au1x00_ic_data->request0_int &
//	    m->md_int.au1x00_ic_data->mask) != 0)
//		cpu_interrupt(cpu, 2);
//	else
//		cpu_interrupt_ack(cpu, 2);

	/*  TODO: What _is_ request1?  */

	/*  TODO: Controller 1  */
}
#endif


/*
 *  dev_au1x00_ic_access():
 *
 *  Interrupt Controller.
 */
DEVICE_ACCESS(au1x00_ic)
{
	struct au1x00_ic_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	/*  TODO  */

	switch (relative_addr) {
	case IC_CONFIG0_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->config0;
		else
			d->config0 |= idata;
		break;
	case IC_CONFIG0_CLEAR:
		if (writeflag == MEM_READ)
			odata = d->config0;
		else
			d->config0 &= ~idata;
		break;
	case IC_CONFIG1_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->config1;
		else
			d->config1 |= idata;
		break;
	case IC_CONFIG1_CLEAR:
		if (writeflag == MEM_READ)
			odata = d->config1;
		else
			d->config1 &= ~idata;
		break;
	case IC_CONFIG2_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->config2;
		else
			d->config2 |= idata;
		break;
	case IC_CONFIG2_CLEAR:	/*  or IC_REQUEST0_INT  */
		if (writeflag == MEM_READ)
			odata = d->request0_int;
		else
			d->config2 &= ~idata;
		break;
	case IC_SOURCE_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->source;
		else
			d->source |= idata;
		break;
	case IC_SOURCE_CLEAR:	/*  or IC_REQUEST1_INT  */
		if (writeflag == MEM_READ)
			odata = d->request1_int;
		else
			d->source &= ~idata;
		break;
	case IC_ASSIGN_REQUEST_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->assign_request;
		else
			d->assign_request |= idata;
		break;
	case IC_ASSIGN_REQUEST_CLEAR:
		if (writeflag == MEM_READ)
			odata = d->assign_request;
		else
			d->assign_request &= ~idata;
		break;
	case IC_WAKEUP_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->wakeup;
		else
			d->wakeup |= idata;
		break;
	case IC_WAKEUP_CLEAR:
		if (writeflag == MEM_READ)
			odata = d->wakeup;
		else
			d->wakeup &= ~idata;
		break;
	case IC_MASK_READ:	/*  READ or SET  */
		if (writeflag == MEM_READ)
			odata = d->mask;
		else
			d->mask |= idata;
		break;
	case IC_MASK_CLEAR:
		if (writeflag == MEM_READ)
			odata = d->mask;
		else
			d->mask &= ~idata;
		break;
	default:
		if (writeflag == MEM_READ) {
			debug("[ au1x00_ic%i: read from 0x%08lx: 0x%08x ]\n",
			    d->ic_nr, (long)relative_addr, odata);
		} else {
			debug("[ au1x00_ic%i: write to  0x%08lx: 0x%08x ]\n",
			    d->ic_nr, (long)relative_addr, idata);
		}
	}

fatal("TODO: legacy interrupt rewrite!\n");
abort();

//	if (writeflag == MEM_WRITE)
//		cpu_interrupt(cpu, 8 + 64);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_au1x00_uart_access():
 *
 *  UART (Serial controllers).
 */
DEVICE_ACCESS(au1x00_uart)
{
	struct au1x00_uart_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case UART_RXDATA:		/*  0x00  */
		odata = console_readchar(d->console_handle);
		break;
	case UART_TXDATA:		/*  0x04  */
		console_putchar(d->console_handle, idata);
		break;
	case UART_INTERRUPT_ENABLE:	/*  0x08  */
		if (writeflag == MEM_READ)
			odata = d->int_enable;
		else
			d->int_enable = idata;
		break;
	case UART_MODEM_CONTROL:	/*  0x18  */
		if (writeflag == MEM_READ)
			odata = d->modem_control;
		else
			d->modem_control = idata;
		break;
	case UART_LINE_STATUS:		/*  0x1c  */
		odata = ULS_TE + ULS_TFE;
		if (console_charavail(d->console_handle))
			odata |= ULS_DR;
		break;
	case UART_CLOCK_DIVIDER:	/*  0x28  */
		break;
	default:
		if (writeflag == MEM_READ) {
			debug("[ au1x00_uart%i: read from 0x%08lx ]\n",
			    d->uart_nr, (long)relative_addr);
		} else {
			debug("[ au1x00_uart%i: write to  0x%08lx: 0x%08llx"
			    " ]\n", d->uart_nr, (long)relative_addr,
			    (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVICE_TICK(au1x00_pc)
{
/*	struct au1x00_pc_data *d = extra;  */

	/*  Periodic ticks at 32768 Hz? TODO  */

fatal("TODO: legacy interrupt rewrite!\n");
abort();

//	if (d->reg[PC_COUNTER_CONTROL/4] & CC_EN1)
//		cpu_interrupt(cpu, 8 + d->irq_nr);
}


/*
 *  dev_au1x00_pc_access():
 *
 *  Programmable Counters.
 */
DEVICE_ACCESS(au1x00_pc)
{
	struct au1x00_pc_data *d = extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ)
		odata = d->reg[relative_addr / sizeof(uint32_t)];
	else
		d->reg[relative_addr / sizeof(uint32_t)] = idata;

	switch (relative_addr) {
	default:
		if (writeflag == MEM_READ) {
			debug("[ au1x00_pc: read from 0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, odata);
		} else {
			debug("[ au1x00_pc: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(au1x00)
{
	struct machine *machine = devinit->machine;
	struct au1x00_ic_data *d_ic0;
	struct au1x00_ic_data *d_ic1;
	struct au1x00_uart_data *d0;
	struct au1x00_uart_data *d1;
	struct au1x00_uart_data *d2;
	struct au1x00_uart_data *d3;
	struct au1x00_pc_data *d_pc;

	d_ic0 = malloc(sizeof(struct au1x00_ic_data));
	d_ic1 = malloc(sizeof(struct au1x00_ic_data));
	d0 = malloc(sizeof(struct au1x00_uart_data));
	d1 = malloc(sizeof(struct au1x00_uart_data));
	d2 = malloc(sizeof(struct au1x00_uart_data));
	d3 = malloc(sizeof(struct au1x00_uart_data));
	d_pc = malloc(sizeof(struct au1x00_pc_data));

	if (d0 == NULL || d1 == NULL || d2 == NULL ||
	    d3 == NULL || d_pc == NULL || d_ic0 == NULL
	    || d_ic1 == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d_ic0, 0, sizeof(struct au1x00_ic_data));
	memset(d_ic1, 0, sizeof(struct au1x00_ic_data));
	memset(d0, 0, sizeof(struct au1x00_uart_data));
	memset(d1, 0, sizeof(struct au1x00_uart_data));
	memset(d2, 0, sizeof(struct au1x00_uart_data));
	memset(d3, 0, sizeof(struct au1x00_uart_data));
	memset(d_pc, 0, sizeof(struct au1x00_pc_data));

	d_ic0->ic_nr = 0;
	d_ic1->ic_nr = 1;

	d0->uart_nr = 0; d0->irq_nr = 0;
	d1->uart_nr = 1; d1->irq_nr = 1;
	d2->uart_nr = 2; d2->irq_nr = 2;
	d3->uart_nr = 3; d3->irq_nr = 3;

	/*  Only allow input on the first UART, by default:  */
	d0->console_handle = console_start_slave(machine, "AU1x00 port 0", 1);
	d1->console_handle = console_start_slave(machine, "AU1x00 port 1", 0);
	d2->console_handle = console_start_slave(machine, "AU1x00 port 2", 0);
	d3->console_handle = console_start_slave(machine, "AU1x00 port 3", 0);
	d0->in_use = 1;
	d1->in_use = 0;
	d2->in_use = 0;
	d3->in_use = 0;

	d_pc->irq_nr = 14;

	memory_device_register(machine->memory, "au1x00_ic0",
	    IC0_BASE, 0x100, dev_au1x00_ic_access, d_ic0, DM_DEFAULT, NULL);
	memory_device_register(machine->memory, "au1x00_ic1",
	    IC1_BASE, 0x100, dev_au1x00_ic_access, d_ic1, DM_DEFAULT, NULL);

	memory_device_register(machine->memory, "au1x00_uart0", UART0_BASE,
	    UART_SIZE, dev_au1x00_uart_access, d0, DM_DEFAULT, NULL);
	memory_device_register(machine->memory, "au1x00_uart1", UART1_BASE,
	    UART_SIZE, dev_au1x00_uart_access, d1, DM_DEFAULT, NULL);
	memory_device_register(machine->memory, "au1x00_uart2", UART2_BASE,
	    UART_SIZE, dev_au1x00_uart_access, d2, DM_DEFAULT, NULL);
	memory_device_register(machine->memory, "au1x00_uart3", UART3_BASE,
	    UART_SIZE, dev_au1x00_uart_access, d3, DM_DEFAULT, NULL);

	memory_device_register(machine->memory, "au1x00_pc", PC_BASE,
	    PC_SIZE+0x8, dev_au1x00_pc_access, d_pc, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_au1x00_pc_tick, d_pc, 15, 0.0);

	return 1;
}


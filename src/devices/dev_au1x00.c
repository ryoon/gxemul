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
 *  $Id: dev_au1x00.c,v 1.2 2004-08-10 14:19:47 debug Exp $
 *  
 *  Au1x00 (eg Au1500) pseudo device. See aureg.h for bitfield details.
 *
 *  The MeshCube uses an Au1500 CPU.
 *
 *  This is basically just a huge TODO. :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"

#include "console.h"
#include "devices.h"
#include "memory.h"

#include "aureg.h"


struct au1x00_uart_data {
	int		uart_nr;
	uint32_t	int_enable;
	uint32_t	modem_control;
};


struct au1x00_pc_data {
	uint32_t	reg[PC_SIZE/4 + 2];
	int		irq_nr;
};


/*
 *  dev_au1x00_ic_access():
 *
 *  Interrupt Controller.
 */
int dev_au1x00_ic_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct au1x00_ic_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  TODO  */

	switch (relative_addr) {
	default:
		if (writeflag == MEM_READ) {
			debug("[ au1x00_ic%i: read from 0x%08lx: 0x%08x ]\n",
			    d->ic_nr, (long)relative_addr, odata);
		} else {
			debug("[ au1x00_ic%i: write to  0x%08lx: 0x%08x ]\n",
			    d->ic_nr, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_au1x00_uart_access():
 *
 *  UART (Serial controllers).
 */
int dev_au1x00_uart_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct au1x00_uart_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case UART_RXDATA:		/*  0x00  */
		odata = console_readchar();
		break;
	case UART_TXDATA:		/*  0x04  */
		console_putchar(idata);
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
		if (console_charavail())
			odata |= ULS_DR;
		break;
	case UART_CLOCK_DIVIDER:	/*  0x28  */
		break;
	default:
		if (writeflag == MEM_READ) {
			debug("[ au1x00_uart%i: read from 0x%08lx ]\n",
			    d->uart_nr, (long)relative_addr);
		} else {
			debug("[ au1x00_uart%i: write to  0x%08lx: 0x%08llx ]\n",
			    d->uart_nr, (long)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_au1x00_pc_tick():
 */
void dev_au1x00_pc_tick(struct cpu *cpu, void *extra)
{
	struct au1x00_pc_data *d = extra;

	if (d->reg[PC_COUNTER_CONTROL/4] & CC_EN1)
		cpu_interrupt(cpu, 8 + d->irq_nr);
	else
		cpu_interrupt_ack(cpu, 8 + d->irq_nr);
}


/*
 *  dev_au1x00_pc_access():
 *
 *  Programmable Counters.
 */
int dev_au1x00_pc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct au1x00_pc_data *d = extra;
	uint64_t idata = 0, odata = 0;

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


/*
 *  dev_au1x00_init():
 */
struct au1x00_ic_data *dev_au1x00_init(struct cpu *cpu, struct memory *mem)
{
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

	d0->uart_nr = 0;
	d1->uart_nr = 1;
	d2->uart_nr = 2;
	d3->uart_nr = 3;

	d_pc->irq_nr = 0;

	memory_device_register(mem, "au1x00_ic0",
	    IC0_BASE, 0x100, dev_au1x00_ic_access, d_ic0);
	memory_device_register(mem, "au1x00_ic1",
	    IC1_BASE, 0x100, dev_au1x00_ic_access, d_ic1);

	memory_device_register(mem, "au1x00_uart0",
	    UART0_BASE, UART_SIZE, dev_au1x00_uart_access, d0);
	memory_device_register(mem, "au1x00_uart1",
	    UART1_BASE, UART_SIZE, dev_au1x00_uart_access, d1);
	memory_device_register(mem, "au1x00_uart2",
	    UART2_BASE, UART_SIZE, dev_au1x00_uart_access, d2);
	memory_device_register(mem, "au1x00_uart3",
	    UART3_BASE, UART_SIZE, dev_au1x00_uart_access, d3);

	memory_device_register(mem, "au1x00_pc",
	    PC_BASE, PC_SIZE + 0x8, dev_au1x00_pc_access, d_pc);
	cpu_add_tickfunction(cpu, dev_au1x00_pc_tick, d_pc, 15);

	return d_ic0;
}


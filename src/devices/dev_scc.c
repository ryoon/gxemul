/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_scc.c,v 1.7 2004-02-26 15:14:21 debug Exp $
 *  
 *  Serial controller on some DECsystems. (Z8530 ?)
 *
 *  TODO:
 *	All 4 lines  (0=kbd, 1=mouse, 2&3 = comm-ports?)
 *	Interrupts (both RX and TX)
 *	DMA?
 *	Keyboard & mouse data as in the 'dc' device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"

#include "sccreg.h"

#define	N_SCC_REGS	16
#define	MAX_QUEUE_LEN	1024

struct scc_data {
	int		irq_nr;
	int		use_fb;

	int		register_select_in_progress;
	int		register_selected;

	unsigned char	scc_register_r[N_SCC_REGS];
	unsigned char	scc_register_w[N_SCC_REGS];

	unsigned char	rx_queue_char[MAX_QUEUE_LEN];
	int		cur_rx_queue_pos_write;
	int		cur_rx_queue_pos_read;
};


/*
 *  Add a character to the receive queue.
 */
static void add_to_rx_queue(struct scc_data *d, unsigned char ch)
{ 
        d->rx_queue_char[d->cur_rx_queue_pos_write]   = ch; 
        d->cur_rx_queue_pos_write ++;
        if (d->cur_rx_queue_pos_write == MAX_QUEUE_LEN)
                d->cur_rx_queue_pos_write = 0;

        if (d->cur_rx_queue_pos_write == d->cur_rx_queue_pos_read)
                fatal("warning: add_to_rx_queue(): rx_queue overrun!\n");
}


int rx_avail(struct scc_data *d)
{
	return d->cur_rx_queue_pos_write != d->cur_rx_queue_pos_read;
}


unsigned char rx_nextchar(struct scc_data *d)
{
	unsigned char ch;
	ch = d->rx_queue_char[d->cur_rx_queue_pos_read];
	d->cur_rx_queue_pos_read++;
	if (d->cur_rx_queue_pos_read == MAX_QUEUE_LEN)
		d->cur_rx_queue_pos_read = 0;
	return ch;
}


/*
 *  dev_scc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_scc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct scc_data *d = (struct scc_data *) extra;
	uint64_t idata = 0, odata = 0;
	int port;

	if (console_charavail())
		add_to_rx_queue(d, console_readchar());

	idata = memory_readmax64(cpu, data, len);

	/*  Status register:  */
	d->scc_register_r[SCC_RR0] |= SCC_RR0_TX_EMPTY;
	d->scc_register_r[SCC_RR0] &= ~SCC_RR0_RX_AVAIL;
	if (rx_avail(d))
		d->scc_register_r[SCC_RR0] |= SCC_RR0_RX_AVAIL;

	/*  No receive errors:  */
	d->scc_register_r[SCC_RR1] = 0;

	port = relative_addr / 8;
	relative_addr &= 7;

	switch (relative_addr) {
	case 1:		/*  command  */
		if (writeflag==MEM_READ) {
			odata =  d->scc_register_r[d->register_selected];

if (d->register_selected == 3)
	d->scc_register_r[SCC_RR3] &= SCC_RR3_TX_IP_A;

			d->register_select_in_progress = 0;
			d->register_selected = 0;
			/*  debug("[ scc: (port %i) read from 0x%08lx ]\n", port, (long)relative_addr);  */
		} else {
			/*  If no register is selected, then select one. Otherwise, write to the selected register.  */
			if (d->register_select_in_progress == 0) {
				d->register_select_in_progress = 1;
				d->register_selected = idata;
				d->register_selected &= (N_SCC_REGS-1);
			} else {
				d->scc_register_w[d->register_selected] = idata;

				d->scc_register_r[SCC_RR12] = d->scc_register_w[SCC_WR12];
				d->scc_register_r[SCC_RR13] = d->scc_register_w[SCC_WR13];

				d->register_select_in_progress = 0;
				d->register_selected = 0;
			}
			/*  debug("[ scc: (port %i) write to  0x%08lx: 0x%08x ]\n", port, (long)relative_addr, idata);  */
		}
		break;
	case 5:		/*  data  */
		if (writeflag==MEM_READ) {
			if (rx_avail(d))
				odata = rx_nextchar(d);
			debug("[ scc: (port %i) read from 0x%08lx: 0x%02x ]\n", port, (long)relative_addr, odata);
		} else {
			/*  debug("[ scc: (port %i) write to  0x%08lx: 0x%08x ]\n", port, (long)relative_addr, idata);  */
			/*  Send the character:  */
			console_putchar(idata);

			/*  Loopback:  */
			if (d->scc_register_w[d->register_selected] & SCC_WR14_LOCAL_LOOPB)
				add_to_rx_queue(d, idata);

			/*  TX interrupt:  */
/*			if (d->scc_register_w[SCC_WR1] & SCC_WR1_TX_IE) {
				d->scc_register_r[SCC_RR3] |= SCC_RR3_TX_IP_A;
				cpu_interrupt(cpu, d->irq_nr);
			}
*/
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ scc: (port %i) read from 0x%08lx ]\n", port, (long)relative_addr);
		} else {
			debug("[ scc: (port %i) write to  0x%08lx: 0x%08x ]\n", port, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_scc_init():
 */
void dev_scc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb)
{
	struct scc_data *d;

	d = malloc(sizeof(struct scc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct scc_data));
	d->irq_nr = irq_nr;
	d->use_fb = use_fb;

	memory_device_register(mem, "scc", baseaddr, DEV_SCC_LENGTH, dev_scc_access, d);
}


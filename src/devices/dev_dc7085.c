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
 *  $Id: dev_dc7085.c,v 1.5 2003-11-08 14:42:25 debug Exp $
 *  
 *  DC7085 serial controller, used in some DECstation models.
 *
 *  TODO:  Quite a lot has been implemented, but there are some things
 *         left;  self-test, the mouse stuff, more keyboard stuff, ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "console.h"
#include "devices.h"

#include "dc7085.h"
#include "lk201.h"		/*  mouse and keyboard stuff  */


#define	MAX_QUEUE_LEN		1500

struct dc_data {
	struct dc7085regs	regs;

	unsigned char		rx_queue_char[MAX_QUEUE_LEN];
	char			rx_queue_lineno[MAX_QUEUE_LEN];
	int			cur_rx_queue_pos_write;
	int			cur_rx_queue_pos_read;

	int			tx_scanner;

	int			irqnr;
	int			use_fb;
};


/*
 *  Add a character to the receive queue.
 */
static void add_to_rx_queue(struct dc_data *d, unsigned char ch, int line_no)
{
	d->rx_queue_char[d->cur_rx_queue_pos_write]   = ch;
	d->rx_queue_lineno[d->cur_rx_queue_pos_write] = line_no;
	d->cur_rx_queue_pos_write ++;
	if (d->cur_rx_queue_pos_write == MAX_QUEUE_LEN)
		d->cur_rx_queue_pos_write = 0;

	if (d->cur_rx_queue_pos_write == d->cur_rx_queue_pos_read)
		fatal("warning: add_to_rx_queue(): rx_queue overrun!\n");
}


void convert_ascii_to_keybcode(struct dc_data *d, unsigned char ch)
{
	int i, found=-1, shifted = 0, controlled = 0;

	switch (ch) {
	case '\b':
		found = 0xbc;
		break;
	case '\n':
		found = 0xbd;
		break;
	case '\t':
		found = 0xbe;
		break;
	default:
		if (ch >= 1 && ch <= 26) {
			ch = 'a' + ch - 1;
			controlled = 1;
		}

		shifted = 0;
		for (i=0; i<256; i++)
			if (unshiftedAscii[i] == ch) {
				found = i;
				break;
			}

		if (found == -1) {
			/*  unshift ch:  */
			if (ch >= 'A' && ch <= 'Z')
				ch = ch + ('a' - 'A');
			for (i=0; i<256; i++)
				if (shiftedAscii[i] == ch) {
					found = i;
					shifted = 1;
					break;
				}
		}
	}

	if (!shifted)
		add_to_rx_queue(d, KEY_UP, DCKBD_PORT);
	else {
		add_to_rx_queue(d, KEY_UP, DCKBD_PORT);
		add_to_rx_queue(d, KEY_SHIFT, DCKBD_PORT);
	}

	if (controlled)
		add_to_rx_queue(d, KEY_CONTROL, DCKBD_PORT);

	add_to_rx_queue(d, found, DCKBD_PORT);
}


/*
 *  dev_dc7085_tick():
 *
 *  This function is called "every now and then".
 *  If a key is available from the keyboard, add it to the rx queue.
 *  If other bits are set, an interrupt might need to be caused.
 */
void dev_dc7085_tick(struct cpu *cpu, void *extra)
{
	struct dc_data *d = extra;
	int avail;

	if (console_charavail()) {
		unsigned char ch = console_readchar();

		/*  Ugly hack:  CTRL-B (host) ==> CTRL-C (emulator)  */
		if (ch == 2)
			ch = 3;

		if (d->use_fb)
			convert_ascii_to_keybcode(d, ch);
		else {
			/*  This is ugly, but neccessary because different machines
				seem to use different ports for their serial console:  */
			add_to_rx_queue(d, ch, DCKBD_PORT);	/*  DEC MIPSMATE 5100  */
			add_to_rx_queue(d, ch, DCCOMM_PORT);
			add_to_rx_queue(d, ch, DCPRINTER_PORT);	/*  DECstation 3100 (PMAX)  */
		}
	}

	avail = d->cur_rx_queue_pos_write != d->cur_rx_queue_pos_read;

	d->regs.dc_csr &= ~CSR_RDONE;
	if (avail && d->regs.dc_csr & CSR_MSE)
		d->regs.dc_csr |= CSR_RDONE;

	if (d->regs.dc_csr & CSR_RDONE && d->regs.dc_csr & CSR_RIE)
		cpu_interrupt(cpu, d->irqnr);

	if (d->regs.dc_csr & CSR_MSE && !(d->regs.dc_csr & CSR_TRDY)) {
		int scanner_start = d->tx_scanner;

		/*  Loop until we've checked all 4 channels, or some
			channel was ready to transmit:  */

		do {
			d->tx_scanner = (d->tx_scanner + 1) % 4;

			if (d->regs.dc_tcr & (1 << d->tx_scanner)) {
				d->regs.dc_csr |= CSR_TRDY;
				if (d->regs.dc_csr & CSR_TIE)
					cpu_interrupt(cpu, d->irqnr);

				d->regs.dc_csr &= ~CSR_TX_LINE_NUM;
				d->regs.dc_csr |= (d->tx_scanner << 8);
			}
		} while (!(d->regs.dc_csr & CSR_TRDY) && d->tx_scanner != scanner_start);
	}
}


/*
 *  dev_dc7085_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dc7085_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	int idata = 0, odata=0, odata_set=0;
	struct dc_data *d = extra;

	dev_dc7085_tick(cpu, extra);

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

	/*  Always clear:  */
	d->regs.dc_csr &= ~CSR_CLR;

	switch (relative_addr) {
	case 0x00:	/*  CSR:  Control and Status  */
		if (writeflag == MEM_WRITE) {
			debug("[ dc7085 write to CSR: 0x%04x ]\n", idata);
			idata &= (CSR_TIE | CSR_RIE | CSR_MSE | CSR_CLR | CSR_MAINT);
			d->regs.dc_csr &= ~(CSR_TIE | CSR_RIE | CSR_MSE | CSR_CLR | CSR_MAINT);
			d->regs.dc_csr |= idata;
			if (!(d->regs.dc_csr & CSR_MSE))
				d->regs.dc_csr &= ~(CSR_TRDY | CSR_RDONE);
			return 1;
		} else {
			/*  read:  */
			/*  debug("[ dc7085 read from CSR: (csr = 0x%04x) ]\n", d->regs.dc_csr);  */
			odata = d->regs.dc_csr;
			odata_set = 1;
		}
		break;
	case 0x08:	/*  LPR:  */
		if (writeflag == MEM_WRITE) {
			debug("[ dc7085 write to LPR: 0x%04x ]\n", idata);
			d->regs.dc_rbuf_lpr = idata;
			return 1;
		} else {
			/*  read:  */
			int avail = d->cur_rx_queue_pos_write != d->cur_rx_queue_pos_read;
			int ch = 0, lineno = 0;
			debug("[ dc7085 read from RBUF: ");
			if (avail) {
				ch     = d->rx_queue_char[d->cur_rx_queue_pos_read];
				lineno = d->rx_queue_lineno[d->cur_rx_queue_pos_read];
				d->cur_rx_queue_pos_read++;
				if (d->cur_rx_queue_pos_read == MAX_QUEUE_LEN)
					d->cur_rx_queue_pos_read = 0;
				if (ch >= ' ' && ch < 127)
					debug("'%c'", ch);
				else
					debug("0x%x", ch);
				debug(" for lineno %i ", lineno);
			} else
				debug("empty ");
			debug("]\n");
			odata = (avail? RBUF_DVAL:0) | (lineno << RBUF_LINE_NUM_SHIFT) | ch;
			odata_set = 1;

			d->regs.dc_csr &= ~CSR_RDONE;
			cpu_interrupt_ack(cpu, d->irqnr);
		}
		break;
	case 0x10:	/*  TCR:  */
		if (writeflag == MEM_WRITE) {
			/*  debug("[ dc7085 write to TCR: 0x%04x) ]\n", idata);  */
			d->regs.dc_tcr = idata;
			d->regs.dc_csr &= ~CSR_TRDY;
			cpu_interrupt_ack(cpu, d->irqnr);
			return 1;
		} else {
			/*  read:  */
			/*  debug("[ dc7085 read from TCR: (tcr = 0x%04x) ]\n", d->regs.dc_tcr);  */
			odata = d->regs.dc_tcr;
			odata_set = 1;
		}
		break;
	case 0x18:	/*  Modem status (R), transmit data (W)  */
		if (writeflag == MEM_WRITE) {
			int line_no = (d->regs.dc_csr >> RBUF_LINE_NUM_SHIFT) & 0x3;
			idata &= 0xff;

			switch (line_no) {
			case DCKBD_PORT:		/*  port 0  */
				if (!d->use_fb) {
					/*  Simply print the character to stdout:  */
					console_putchar(idata);
				} else {
					debug("[ dc7085 keyboard control: 0x%x ]\n", idata);
				}
				break;
			case DCMOUSE_PORT:		/*  port 1  */
				debug("[ dc7085 writing data to MOUSE: 0x%x", idata);
				if (idata == MOUSE_INCREMENTAL) {
					/*  TODO  */
				}
				if (idata == MOUSE_SELF_TEST) {
					/*
					 *  Mouse self-test:
					 *
					 *  TODO: Find out if this is correct. The lowest
					 *        four bits of the second byte should be
					 *        0x2, according to NetBSD/pmax. But the
					 *        other bits and bytes?
					 */
					debug(" (mouse self-test request)");
					add_to_rx_queue(d, 0x01, DCMOUSE_PORT);
					add_to_rx_queue(d, 0x02, DCMOUSE_PORT);
					add_to_rx_queue(d, 0x03, DCMOUSE_PORT);
					add_to_rx_queue(d, 0x04, DCMOUSE_PORT);
				}
				debug(" ]\n");
				break;
			case DCCOMM_PORT:		/*  port 2  */
			case DCPRINTER_PORT:		/*  port 3  */
				/*  Simply print the character to stdout:  */
				console_putchar(idata);
			}

			d->regs.dc_csr &= ~CSR_TRDY;
			cpu_interrupt_ack(cpu, d->irqnr);

			return 1;
		} else {
			/*  read:  */
			d->regs.dc_msr_tdr |= MSR_DSR2 | MSR_CD2 | MSR_DSR3 | MSR_CD3;
			debug("[ dc7085 read from MSR: (msr_tdr = 0x%04x) ]\n", d->regs.dc_msr_tdr);
			odata = d->regs.dc_msr_tdr;
			odata_set = 1;
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ dc7085 read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ dc7085 write to 0x%08lx:", (long)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
		}
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
		return 1;
	}

	return 0;
}


/*
 *  dev_dc7085_init():
 *
 *  Initialize a dc7085 serial controller device.
 *  use_fb should be non-zero if a framebuffer device is used.
 *  Channel 0 will then be treated as a DECstation keyboard,
 *  instead of a plain serial console.
 */
void dev_dc7085_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb)
{
	struct dc_data *d;

	d = malloc(sizeof(struct dc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dc_data));
	d->irqnr  = irq_nr;
	d->use_fb = use_fb;

	d->regs.dc_csr = CSR_TRDY | CSR_MSE;
	d->regs.dc_tcr = 0x00;

	memory_device_register(mem, "dc7085", baseaddr, DEV_DC7085_LENGTH, dev_dc7085_access, d);
	cpu_add_tickfunction(cpu, dev_dc7085_tick, d, 9);		/*  every 512:th cycle  */
}


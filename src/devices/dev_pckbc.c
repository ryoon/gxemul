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
 *  $Id: dev_pckbc.c,v 1.14 2004-06-29 02:30:34 debug Exp $
 *  
 *  Standard 8042 PC keyboard controller, and a 8242WB PS2 keyboard/mouse
 *  controller.
 *
 *  TODO:  Actually handle keypresses :-)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


#define	MAX_8042_QUEUELEN	256

#define	PS2_TXBUF		0
#define	PS2_RXBUF		1
#define	PS2_CONTROL		2
#define	PS2_STATUS		3

#define	PS2	100

struct pckbc_data {
	int		reg[DEV_PCKBC_LENGTH];
	int		keyboard_irqnr;
	int		mouse_irqnr;
	int		type;

	/*  TODO: one of these for each port?  */
	int		clocksignal;
	int		rx_int_enable;
	int		tx_int_enable;

	unsigned	key_queue[2][MAX_8042_QUEUELEN];
	int		head[2], tail[2];
};


/*
 *  pckbc_add_code():
 *
 *  Adds a byte to the data queue.
 */
void pckbc_add_code(struct pckbc_data *d, int code, int port)
{
	/*  Add at the head, read at the tail:  */
	d->head[port] = (d->head[port]+1) % MAX_8042_QUEUELEN;
	if (d->head[port] == d->tail[port])
		fatal("[ pckbc: queue overrun, port %i! ]\n", port);

	d->key_queue[port][d->head[port]] = code;
}


/*
 *  pckbc_get_code():
 *
 *  Reads a byte from a data queue.
 */
int pckbc_get_code(struct pckbc_data *d, int port)
{
	if (d->head[port] == d->tail[port])
		fatal("[ pckbc: queue empty, port %i! ]\n", port);

	d->tail[port] = (d->tail[port]+1) % MAX_8042_QUEUELEN;
	return d->key_queue[port][d->tail[port]];
}


/*
 *  dev_pckbc_tick():
 */
void dev_pckbc_tick(struct cpu *cpu, void *extra)
{
	struct pckbc_data *d = extra;
	int port_nr;

	for (port_nr=0; port_nr<2; port_nr++) {
		/*  Cause receive interrupt, if there's something in the receive buffer:  */
		if (d->head[port_nr] != d->tail[port_nr] && d->rx_int_enable) {
			cpu_interrupt(cpu, port_nr==0? d->keyboard_irqnr : d->mouse_irqnr);
		} else {
			cpu_interrupt_ack(cpu, port_nr==0? d->keyboard_irqnr : d->mouse_irqnr);
		}
	}
}


/*
 *  dev_pckbc_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_pckbc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, code, port_nr = 0;
	struct pckbc_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*
	 *  TODO:  this is almost 100% dummy
	 */

	/*  8242 PS2-style:  */
	if (d->type == PCKBC_8242) {
		relative_addr /= sizeof(uint64_t);	/*  if using 8-byte alignment  */
		port_nr = (relative_addr >> 2);		/*  0 for keyboard, 1 for mouse  */
		relative_addr &= 3;
		relative_addr += PS2;
	}

	switch (relative_addr) {

	/*
	 *  8042 (PC):
	 */

	case 0:		/*  data  */
		if (writeflag==MEM_READ) {
			odata = 0;
			if (d->head[0] != d->tail[0])
				odata = pckbc_get_code(d, 0);
			debug("[ pckbc: read from DATA: 0x%02x ]\n", odata);
		} else {
			debug("[ pckbc: write to DATA:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
		break;
	case 1:		/*  control  */
		if (writeflag==MEM_READ) {
			odata = 0;

			/*  "Data in buffer" bit  */
			if (d->head[0] != d->tail[0])
				odata |= 1;

			/*  debug("[ pckbc: read from CTL: 0x%02x ]\n", odata);  */
		} else {
			debug("[ pckbc: write to CTL:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;

			code = 0xfa; /*  KBR_ACK;  */

			/*  Self-test:  */
			if (idata == 0xaa)
				code = 0x55;

			pckbc_add_code(d, code, 0);
		}
		break;

	/*
	 *  8242 (PS2):
	 */

	case PS2 + PS2_TXBUF:
		if (writeflag==MEM_READ) {
			odata = random() & 0xff;
			debug("[ pckbc: read from port %i, PS2_TXBUF: 0x%x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_TXBUF: 0x%llx ]\n", port_nr, (long long)idata);

			/*  Handle keyboard commands:  */
			switch (idata) {
			/*  These are incorrect, the second byte of commands should be treated better:  */
			case 0x00:	/*  second byte of 0xed, SGI-IP32's prom  */
				pckbc_add_code(d, 0x03, port_nr);	/*  ack  (?) */
				break;
			case 0x14:	/*  second byte of 0xfc, SGI-IP32's prom  */
			case 0x28:	/*  second byte of 0xf3, SGI-IP32's prom  */
			case 0x76:	/*  third byte of 0xfc, SGI-IP32's prom  */
			case 0x03:	/*  second byte of ATKBD_CMD_GSCANSET (?)  */
			case 0x04:
				pckbc_add_code(d, 0x03, port_nr);	/*  ?  */
				break;

			/*  Command bytes:  */
			case 0xf0:	/*  ATKBD_CMD_GSCANSET (?)  */
				pckbc_add_code(d, 0x03, port_nr);	/*  ?  */
				break;
			case 0xf2:	/*  Get keyboard ID  */
				/*  The keyboard should generate 2 status bytes.  */
				pckbc_add_code(d, 0xab, port_nr);
				pckbc_add_code(d, 0x83, port_nr);
				break;
			case 0xed:	/*  "ATKBD_CMD_SETLEDS", takes 1 byte arg  */
			case 0xf3:	/*  "PSMOUSE_CMD_SETRATE", takes 1 byte arg  */
			case 0xf4:	/*  "ATKBD_CMD_ENABLE" (or PSMOUSE_CMD_ENABLE), no args  */
			case 0xf5:	/*  "ATKBD_CMD_RESET_DIS" (keyboard, according to Linux sources)  */
			case 0xf6:	/*  "PSMOUSE_CMD_RESET_DIS" (mouse, according to Linux sources)  */
				/*  TODO: what does this do?  */
				pckbc_add_code(d, 0xfa, port_nr);	/*  ack  (?) */
				break;
			case 0xfa:	/*  "ATKBD_CMD_SETALL_MBR" (linux)  */
				pckbc_add_code(d, 0xfa, port_nr);	/*  ack  (?) */
				break;
			case 0xfc:	/*  ?  */
				pckbc_add_code(d, 0xfa, port_nr);	/*  ack  (?) */
				break;
			case 0xff:	/*  Keyboard reset  */
				/*  The keyboard should generate 2 status bytes.  */
				pckbc_add_code(d, 0xfa, port_nr);	/*  ack  (?) */
				pckbc_add_code(d, 0xaa, port_nr);	/*  battery ok (?)  */
				break;
			default:
				debug("[ pckbc: UNIMPLEMENTED keyboard command 0x%02x (port %i) ]\n", (int)idata, port_nr);
			}
		}
		break;

	case PS2 + PS2_RXBUF:
		if (writeflag==MEM_READ) {
			odata = random() & 0xff;	/*  what to return if no data is available? TODO  */
			if (d->head[port_nr] != d->tail[port_nr])
				odata = pckbc_get_code(d, port_nr);
			debug("[ pckbc: read from port %i, PS2_RXBUF: 0x%02x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_RXBUF: 0x%llx ]\n", port_nr, (long long)idata);
		}
		break;

	case PS2 + PS2_CONTROL:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from port %i, PS2_CONTROL ]\n", port_nr);
		} else {
			debug("[ pckbc: write to port %i, PS2_CONTROL: 0x%llx ]\n", port_nr, (long long)idata);
			d->clocksignal = (idata & 0x10) ? 1 : 0;
			d->rx_int_enable = (idata & 0x08) ? 1 : 0;
			d->tx_int_enable = (idata & 0x04) ? 1 : 0;
		}
		break;

	case PS2 + PS2_STATUS:
		if (writeflag==MEM_READ) {
			odata = d->clocksignal + 0x08;	/* 0x08 = transmit buffer empty  */

			if (d->head[port_nr] != d->tail[port_nr])
				odata |= 0x10;		/*  0x10 = receicer data available (?)  */

			debug("[ pckbc: read from port %i, PS2_STATUS: 0x%llx ]\n", port_nr, (long long)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_STATUS: 0x%llx ]\n", port_nr, (long long)idata);
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from unimplemented reg %i ]\n",
			    (int)relative_addr);
			odata = d->reg[relative_addr];
		} else {
			debug("[ pckbc: write to unimplemented reg %i:",
			    (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	dev_pckbc_tick(cpu, d);

	return 1;
}


/*
 *  dev_pckbc_init():
 *
 *  Type should be PCKBC_8042 or PCKBC_8242.
 */
void dev_pckbc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	int type, int keyboard_irqnr, int mouse_irqnr)
{
	struct pckbc_data *d;

	d = malloc(sizeof(struct pckbc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pckbc_data));
	d->type           = type;
	d->keyboard_irqnr = keyboard_irqnr;
	d->mouse_irqnr    = mouse_irqnr;

	memory_device_register(mem, "pckbc", baseaddr,
	    DEV_PCKBC_LENGTH, dev_pckbc_access, d);
	cpu_add_tickfunction(cpu, dev_pckbc_tick, d, 10);
}


/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_pckbc.c,v 1.39 2005-05-15 22:44:41 debug Exp $
 *  
 *  Standard 8042 PC keyboard controller, and a 8242WB PS2 keyboard/mouse
 *  controller.
 *
 *
 *  TODO: Finish the rewrite for 8242.
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

#include "kbdreg.h"


/*  #define PCKBC_DEBUG  */


#define	MAX_8042_QUEUELEN	256

#define	PC_DATA			0
#define	PC_CMD			0
#define	PC_STATUS		1

#define	PS2_TXBUF		0
#define	PS2_RXBUF		1
#define	PS2_CONTROL		2
#define	PS2_STATUS		3

#define	PS2	100

#define	PCKBC_TICKSHIFT		14

struct pckbc_data {
	int		console_handle;
	int		in_use;

	int		reg[DEV_PCKBC_LENGTH];
	int		keyboard_irqnr;
	int		mouse_irqnr;
	int		type;

	/*  TODO: one of these for each port?  */
	int		clocksignal;
	int		rx_int_enable;
	int		tx_int_enable;

	int		keyscanning_enabled;
	int		state;
	int		cmdbyte;
	int		last_scancode;

	unsigned	key_queue[2][MAX_8042_QUEUELEN];
	int		head[2], tail[2];
};

#define	STATE_NORMAL			0
#define	STATE_LDCMDBYTE			1
#define	STATE_RDCMDBYTE			2
#define	STATE_WAITING_FOR_TRANSLTABLE	3


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
 *  ascii_to_scancodes():
 *
 *  Conversion from ASCII codes to default (US) keyboard scancodes.
 *  (See http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html)
 */
static void ascii_to_pc_scancodes(int a, struct pckbc_data *d)
{
	int p = 0;	/*  port  */
	int shift = 0, ctrl = 0;

	if (a >= 'A' && a <= 'Z') { a += 32; shift = 1; }
	if ((a >= 1 && a <= 26) && (a!='\n' && a!='\t' && a!='\b' && a!='\r'))
		{ a += 96; ctrl = 1; }

	if (shift)
		pckbc_add_code(d, 0x2a, p);
	else
		pckbc_add_code(d, 0x2a + 0x80, p);

	if (ctrl)
		pckbc_add_code(d, 0x1d, p);

	/*
	 *  TODO: Release for all of these?
	 */

	if (a==27)	pckbc_add_code(d, 0x01, p);

	if (a=='1')	pckbc_add_code(d, 0x02, p);
	if (a=='2')	pckbc_add_code(d, 0x03, p);
	if (a=='3')	pckbc_add_code(d, 0x04, p);
	if (a=='4')	pckbc_add_code(d, 0x05, p);
	if (a=='5')	pckbc_add_code(d, 0x06, p);
	if (a=='6')	pckbc_add_code(d, 0x07, p);
	if (a=='7')	pckbc_add_code(d, 0x08, p);
	if (a=='8')	pckbc_add_code(d, 0x09, p);
	if (a=='9')	pckbc_add_code(d, 0x0a, p);
	if (a=='0')	pckbc_add_code(d, 0x0b, p);
	if (a=='-')	pckbc_add_code(d, 0x0c, p);
	if (a=='=')	pckbc_add_code(d, 0x0d, p);

	if (a=='!')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x02, p); }
	if (a=='@')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x03, p); }
	if (a=='#')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x04, p); }
	if (a=='$')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x05, p); }
	if (a=='%')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x06, p); }
	if (a=='^')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x07, p); }
	if (a=='&')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x08, p); }
	if (a=='*')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x09, p); }
	if (a=='(')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x0a, p); }
	if (a==')')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x0b, p); }
	if (a=='_')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x0c, p); }
	if (a=='+')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x0d, p); }

	if (a=='\b')	pckbc_add_code(d, 0x0e, p);

	if (a=='\t')	pckbc_add_code(d, 0x0f, p);
	if (a=='q')	pckbc_add_code(d, 0x10, p);
	if (a=='w')	pckbc_add_code(d, 0x11, p);
	if (a=='e')	pckbc_add_code(d, 0x12, p);
	if (a=='r')	pckbc_add_code(d, 0x13, p);
	if (a=='t')	pckbc_add_code(d, 0x14, p);
	if (a=='y')	pckbc_add_code(d, 0x15, p);
	if (a=='u')	pckbc_add_code(d, 0x16, p);
	if (a=='i')	pckbc_add_code(d, 0x17, p);
	if (a=='o')	pckbc_add_code(d, 0x18, p);
	if (a=='p')	pckbc_add_code(d, 0x19, p);

	if (a=='[')	pckbc_add_code(d, 0x1a, p);
	if (a=='{')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x1a, p); }
	if (a==']')	pckbc_add_code(d, 0x1b, p);
	if (a=='}')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x1b, p); }

	if (a=='\n' || a=='\r')	pckbc_add_code(d, 0x1c, p);

	if (a=='a')	pckbc_add_code(d, 0x1e, p);
	if (a=='s')	pckbc_add_code(d, 0x1f, p);
	if (a=='d')	pckbc_add_code(d, 0x20, p);
	if (a=='f')	pckbc_add_code(d, 0x21, p);
	if (a=='g')	pckbc_add_code(d, 0x22, p);
	if (a=='h')	pckbc_add_code(d, 0x23, p);
	if (a=='j')	pckbc_add_code(d, 0x24, p);
	if (a=='k')	pckbc_add_code(d, 0x25, p);
	if (a=='l')	pckbc_add_code(d, 0x26, p);

	if (a==';')	pckbc_add_code(d, 0x27, p);
	if (a==':')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x27, p); }
	if (a=='\'')	pckbc_add_code(d, 0x28, p);
	if (a=='"')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x28, p); }
	if (a=='~')	pckbc_add_code(d, 0x29, p);

	if (a=='\\')	pckbc_add_code(d, 0x2b, p);
	if (a=='|')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x2b, p); }

	if (a=='z')	pckbc_add_code(d, 0x2c, p);
	if (a=='x')	pckbc_add_code(d, 0x2d, p);
	if (a=='c')	pckbc_add_code(d, 0x2e, p);
	if (a=='v')	pckbc_add_code(d, 0x2f, p);
	if (a=='b')	pckbc_add_code(d, 0x30, p);
	if (a=='n')	pckbc_add_code(d, 0x31, p);
	if (a=='m')	pckbc_add_code(d, 0x32, p);

	if (a==',')	pckbc_add_code(d, 0x33, p);
	if (a=='<')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x33, p); }
	if (a=='.')	pckbc_add_code(d, 0x34, p);
	if (a=='>')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x34, p); }
	if (a=='/')	pckbc_add_code(d, 0x35, p);
	if (a=='?')  {	pckbc_add_code(d, 0x2a, p);
			pckbc_add_code(d, 0x35, p); }

	if (a==' ')	pckbc_add_code(d, 0x39, p);

	/*  Release ctrl:  */
	if (ctrl)
		pckbc_add_code(d, 0x1d + 0x80, p);
}


/*
 *  dev_pckbc_tick():
 */
void dev_pckbc_tick(struct cpu *cpu, void *extra)
{
	struct pckbc_data *d = extra;
	int port_nr;
	int ch;

	if (d->in_use && console_charavail(d->console_handle)) {
		ch = console_readchar(d->console_handle);
		if (ch >= 0)
			ascii_to_pc_scancodes(ch, d);
	}

	/*  TODO: mouse movements?  */

	for (port_nr=0; port_nr<2; port_nr++) {
		/*  Cause receive interrupt,
		    if there's something in the receive buffer:  */
		if (d->head[port_nr] != d->tail[port_nr] && d->rx_int_enable) {
			cpu_interrupt(cpu, port_nr==0? d->keyboard_irqnr
			    : d->mouse_irqnr);
		} else {
			cpu_interrupt_ack(cpu, port_nr==0? d->keyboard_irqnr
			    : d->mouse_irqnr);
		}
	}
}


/*
 *  dev_pckbc_command():
 */
static void dev_pckbc_command(struct pckbc_data *d, int port_nr)
{
	int cmd = d->reg[PC_CMD];

	if (d->type == PCKBC_8242)
		cmd = d->reg[PS2_TXBUF];

	if (d->state == STATE_WAITING_FOR_TRANSLTABLE) {
		debug("[ pckbc: switching to translation table 0x%02x ]\n",
		    cmd);
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_NORMAL;
		return;
	}

	switch (cmd) {
	case 0x00:
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;
	case KBC_MODEIND:	/*  Set LEDs  */
		/*  Just ACK, no LEDs are actually set.  */
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;
	case KBC_SETTABLE:
		pckbc_add_code(d, KBR_ACK, port_nr);
		d->state = STATE_WAITING_FOR_TRANSLTABLE;
		break;
	case KBC_ENABLE:
		d->keyscanning_enabled = 1;
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;
	case KBC_DISABLE:
		d->keyscanning_enabled = 0;
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;
	case KBC_SETDEFAULT:
		pckbc_add_code(d, KBR_ACK, port_nr);
		break;
	case KBC_RESET:
		pckbc_add_code(d, KBR_ACK, port_nr);
		pckbc_add_code(d, KBR_RSTDONE, port_nr);
		break;
	default:
		fatal("[ pckbc: UNIMPLEMENTED command 0x%02x ]\n", cmd);
	}
}


/*
 *  dev_pckbc_access():
 */
int dev_pckbc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, port_nr = 0;
	struct pckbc_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

#ifdef PCKBC_DEBUG
	if (writeflag == MEM_WRITE)
		fatal("[ pckbc: write to addr 0x%x: 0x%x ]\n",
		    (int)relative_addr, (int)idata);
	else
		fatal("[ pckbc: read from addr 0x%x ]\n",
		    (int)relative_addr);
#endif

	/*  For JAZZ-based machines:  */
	if (relative_addr >= 0x60)
		relative_addr -= 0x60;

	/*  8242 PS2-style:  */
	if (d->type == PCKBC_8242) {
		/*  when using 8-byte alignment...  */
		relative_addr /= sizeof(uint64_t);
		/*  port_nr = 0 for keyboard, 1 for mouse  */
		port_nr = (relative_addr >> 2);
		relative_addr &= 3;
		relative_addr += PS2;
	} else {
		/*  The relative_addr is either 0 or 1,
		    but some machines use longer registers than one byte
		    each, so this will make things simpler for us:  */
		if (relative_addr)
			relative_addr = 1;
	}

	switch (relative_addr) {

	/*
	 *  8042 (PC):
	 */

	case 0:		/*  data  */
		if (writeflag==MEM_READ) {
			if (d->state == STATE_RDCMDBYTE) {
				odata = d->cmdbyte;
				d->state = STATE_NORMAL;
			} else {
				if (d->head[0] != d->tail[0]) {
					odata = pckbc_get_code(d, 0);
					d->last_scancode = odata;
				} else {
					odata = d->last_scancode;
					d->last_scancode |= 0x80;
				}
			}
			debug("[ pckbc: read from DATA: 0x%02x ]\n", odata);
		} else {
			debug("[ pckbc: write to DATA:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");

			if (d->state == STATE_LDCMDBYTE) {
				d->cmdbyte = idata;
				d->rx_int_enable = d->cmdbyte &
				    (KC8_KENABLE | KC8_MENABLE) ? 1 : 0;
				d->state = STATE_NORMAL;
			} else {
				d->reg[relative_addr] = idata;
				dev_pckbc_command(d, port_nr);
			}
		}
		break;
	case 1:		/*  control  */
		if (writeflag==MEM_READ) {
			dev_pckbc_tick(cpu, d);

			odata = 0;

			/*  "Data in buffer" bit  */
			if (d->head[0] != d->tail[0] ||
			    d->state == STATE_RDCMDBYTE)
				odata |= KBS_DIB;
			/*  odata |= KBS_OCMD;  */
			/*  debug("[ pckbc: read from CTL status port: "
			    "0x%02x ]\n", (int)odata);  */
		} else {
			debug("[ pckbc: write to CTL:");
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;

			switch (idata) {
			case K_RDCMDBYTE:
				d->state = STATE_RDCMDBYTE;
				break;
			case K_LDCMDBYTE:
				d->state = STATE_LDCMDBYTE;
				break;
			case 0xa9:	/*  test auxiliary port  */
				debug("[ pckbc: CONTROL 0xa9, TODO ]\n");
				break;
			case 0xaa:	/*  keyboard self-test  */
				pckbc_add_code(d, 0x55, port_nr);
				break;
			case 0xd4:	/*  write to auxiliary port  */
				debug("[ pckbc: CONTROL 0xd4, TODO ]\n");
				break;
			default:
				fatal("[ pckbc: unknown CONTROL 0x%x ]\n",
				    idata);
				d->state = STATE_NORMAL;
			}
		}
		break;

	/*
	 *  8242 (PS2):
	 */

/*
 *  BIG TODO: The following should be rewritten to use dev_pckbc_command()
 *  etc, like the 8042 code above does.
 */

	case PS2 + PS2_TXBUF:
		if (writeflag==MEM_READ) {
			odata = random() & 0xff;
			debug("[ pckbc: read from port %i, PS2_TXBUF: "
			    "0x%x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_TXBUF: "
			    "0x%llx ]\n", port_nr, (long long)idata);

			/*  Handle keyboard commands:  */
			switch (idata) {
			/*  These are incorrect, the second byte of
			    commands should be treated better:  */
			case 0x00:	/*  second byte of 0xed,
					    SGI-IP32's prom  */
				pckbc_add_code(d, 0x03, port_nr);/*  ack  (?) */
				break;
			case 0x14:	/*  second byte of 0xfc,
					    SGI-IP32's prom  */
			case 0x28:	/*  second byte of 0xf3,
					    SGI-IP32's prom  */
			case 0x76:	/*  third byte of 0xfc,
					    SGI-IP32's prom  */
			case 0x03:	/*  second byte of
					    ATKBD_CMD_GSCANSET (?)  */
			case 0x04:
				pckbc_add_code(d, 0x03, port_nr);/*  ?  */
				break;

			/*  Command bytes:  */
			case 0xf0:	/*  ATKBD_CMD_GSCANSET (?)  */
				pckbc_add_code(d, 0x03, port_nr);/*  ?  */
				break;
			case 0xf2:	/*  Get keyboard ID  */
				/*  The keyboard should generate 2
				    status bytes.  */
				pckbc_add_code(d, 0xab, port_nr);
				pckbc_add_code(d, 0x83, port_nr);
				break;
			case 0xed:	/*  "ATKBD_CMD_SETLEDS",
					    takes 1 byte arg  */
			case 0xf3:	/*  "PSMOUSE_CMD_SETRATE",
					    takes 1 byte arg  */
			case 0xf4:	/*  "ATKBD_CMD_ENABLE" (or
					    PSMOUSE_CMD_ENABLE), no args  */
			case 0xf5:	/*  "ATKBD_CMD_RESET_DIS" (keyboard,
					    according to Linux sources)  */
			case 0xf6:	/*  "PSMOUSE_CMD_RESET_DIS" (mouse,
					    according to Linux sources)  */
				/*  TODO: what does this do?  */
				pckbc_add_code(d, 0xfa, port_nr);/*  ack  (?) */
				break;
			case 0xfa:	/*  "ATKBD_CMD_SETALL_MBR" (linux)  */
				pckbc_add_code(d, 0xfa, port_nr);/*  ack  (?) */
				break;
			case 0xfc:	/*  ?  */
				pckbc_add_code(d, 0xfa, port_nr);/*  ack  (?) */
				break;
			case 0xff:	/*  Keyboard reset  */
				/*  The keyboard should generate 2
				    status bytes.  */
				pckbc_add_code(d, 0xfa, port_nr);/*  ack  (?) */
				pckbc_add_code(d, 0xaa, port_nr);
					/*  battery ok (?)  */
				break;
			default:
				debug("[ pckbc: UNIMPLEMENTED keyboard command"
				    " 0x%02x (port %i) ]\n", (int)idata,
				    port_nr);
			}
		}
		break;

	case PS2 + PS2_RXBUF:
		if (writeflag==MEM_READ) {
			/*  TODO: What should be returned if no data 
			    is available?  */
			odata = random() & 0xff;
			if (d->head[port_nr] != d->tail[port_nr])
				odata = pckbc_get_code(d, port_nr);
			debug("[ pckbc: read from port %i, PS2_RXBUF: "
			    "0x%02x ]\n", port_nr, (int)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_RXBUF: "
			    "0x%llx ]\n", port_nr, (long long)idata);
		}
		break;

	case PS2 + PS2_CONTROL:
		if (writeflag==MEM_READ) {
			debug("[ pckbc: read from port %i, PS2_CONTROL"
			    " ]\n", port_nr);
		} else {
			debug("[ pckbc: write to port %i, PS2_CONTROL:"
			    " 0x%llx ]\n", port_nr, (long long)idata);
			d->clocksignal = (idata & 0x10) ? 1 : 0;
			d->rx_int_enable = (idata & 0x08) ? 1 : 0;
			d->tx_int_enable = (idata & 0x04) ? 1 : 0;
		}
		break;

	case PS2 + PS2_STATUS:
		if (writeflag==MEM_READ) {
			/* 0x08 = transmit buffer empty  */
			odata = d->clocksignal + 0x08;

			if (d->head[port_nr] != d->tail[port_nr]) {
				/*  0x10 = receicer data available (?)  */
				odata |= 0x10;
			}

			debug("[ pckbc: read from port %i, PS2_STATUS: "
			    "0x%llx ]\n", port_nr, (long long)odata);
		} else {
			debug("[ pckbc: write to port %i, PS2_STATUS: "
			    "0x%llx ]\n", port_nr, (long long)idata);
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
int dev_pckbc_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int type, int keyboard_irqnr, int mouse_irqnr,
	int in_use)
{
	struct pckbc_data *d;
	int len = DEV_PCKBC_LENGTH;

	d = malloc(sizeof(struct pckbc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pckbc_data));

	if (type == PCKBC_JAZZ) {
		type = PCKBC_8042;
		len = DEV_PCKBC_LENGTH + 0x60;
	}

	d->type           = type;
	d->keyboard_irqnr = keyboard_irqnr;
	d->mouse_irqnr    = mouse_irqnr;
	d->in_use         = in_use;
	d->console_handle = console_start_slave_inputonly(machine, "pckbc");
	d->rx_int_enable  = 1;

	memory_device_register(mem, "pckbc", baseaddr,
	    len, dev_pckbc_access, d, MEM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_pckbc_tick, d, PCKBC_TICKSHIFT);

	return d->console_handle;
}


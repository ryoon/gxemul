/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_dreamcast_maple.c,v 1.1 2006-10-27 04:22:44 debug Exp $
 *  
 *  Dreamcast "Maple" bus controller.
 *
 *  The Maple bus has 4 ports (A-D), and each port has up to 6 possible "units"
 *  (where unit 0 is the main unit). Communication is done using DMA; each
 *  DMA transfer sends one or more requests, and for each of these requests
 *  a response is generated (or a timeout if there was no device at the
 *  specific port).
 *
 *  TODO:
 *	"Registry" for devices (e.g. attach a two Controllers, one Keyboard,
 *		and one microphone?)
 *	DMA RESPONSES!!!
 *	Unit numbers / IDs for real Maple devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "dreamcast_maple.h"


#define debug fatal

struct dreamcast_maple_data {
	uint32_t	dmaaddr;
	int		enable;
	int		timeout;
};


/*
 *  maple_do_dma_xfer():
 *
 *  Perform a DMA transfer. enable should be 1, and dmaaddr should point to
 *  the memory to transfer.
 */
void maple_do_dma_xfer(struct cpu *cpu, struct dreamcast_maple_data *d)
{
	uint32_t addr = d->dmaaddr;

	if (!d->enable) {
		fatal("[ maple_do_dma_xfer: not enabled? ]\n");
		return;
	}

	debug("[ dreamcast_maple: DMA transfer, dmaaddr = "
	    "0x%08"PRIx32" ]\n", addr);

	/*
	 *  DMA transfers must be 32-byte aligned, according to Marcus
	 *   Comstedt's Maple demo program.
	 */
	if (addr & 0x1f) {
		fatal("[ dreamcast_maple: dmaaddr 0x%08"PRIx32" is NOT"
		    " 32-byte aligned; aborting ]\n", addr);
		return;
	}

	/*
	 *  Handle one or more requests/responses:
	 *
	 *  (This is "reverse engineered" from Comstedt's maple demo program;
	 *  it might not be good enough to emulate how the Maple is being
	 *  used by other programs.)
	 */
	for (;;) {
		uint8_t buf[8];
		uint32_t receive_addr;
		int datalen, port, last_message, cmd, to, from, datalen_cmd;
		int unit;

		/*  Read the message' two control words:  */
		cpu->memory_rw(cpu, cpu->mem, addr, (void *) &buf, 8, MEM_READ,
		    NO_EXCEPTIONS | PHYSICAL);
		addr += 8;

		datalen = buf[0] * sizeof(uint32_t);
		port = buf[2];
		last_message = buf[3] & 0x80;
		receive_addr = buf[4] + (buf[5] << 8) + (buf[6] << 16)
		    + (buf[7] << 24);

		if (receive_addr & 0xf000001f)
			fatal("[ dreamcast_maple: WARNING! receive address 0x"
			    "%08"PRIx32" isn't valid! ]\n", receive_addr);

		/*  Read the command word for this message:  */
		cpu->memory_rw(cpu, cpu->mem, addr, (void *) &buf, 4, MEM_READ,
		    NO_EXCEPTIONS | PHYSICAL);
		addr += 4;

		cmd = buf[0];
		to = buf[1];
		from = buf[2];
		datalen_cmd = buf[3];

		/*  Decode the unit number:  */
		unit = 0;
		switch (from & 0x2f) {
		case 0x00:
		case 0x20: unit = 0; break;
		case 0x01: unit = 1; break;
		case 0x02: unit = 2; break;
		case 0x04: unit = 3; break;
		case 0x08: unit = 4; break;
		case 0x10: unit = 5; break;
		default: fatal("[ dreamcast_maple: WARNING! multiple "
			    "units? Not yet implemented. from=0x%02x ]\n",
			    from);
		}

		debug("[ dreamcast_maple: cmd=0x%02x, port=%c, unit=%i"
		    ", datalen=%i words ]\n", cmd, port+'A', unit, datalen_cmd);

		/*
		 *  Handle the command:
		 */
		switch (cmd) {

		case MAPLE_COMMAND_DEVINFO:
			fatal("[ dreamcast_maple: DEVINFO: TODO ]\n");
			break;

		default:fatal("[ dreamcast_maple: command %i: TODO ]\n", cmd);
			exit(1);
		}

		/*  Last request? Then stop.  */
		if (last_message)
			break;
	}
}


DEVICE_ACCESS(dreamcast_maple)
{
	struct dreamcast_maple_data *d = (struct dreamcast_maple_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case 0x04:	/*  MAPLE_DMAADDR  */
		if (writeflag == MEM_WRITE) {
			d->dmaaddr = idata;
			/*  debug("[ dreamcast_maple: dmaaddr set to 0x%08x"
			    " ]\n", d->dmaaddr);  */
		} else {
			fatal("[ dreamcast_maple: TODO: read from dmaaddr ]\n");
			odata = d->dmaaddr;
		}
		break;

	case 0x10:	/*  MAPLE_RESET2  */
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ dreamcast_maple: UNIMPLEMENTED reset2 value"
			    " 0x%08x ]\n", (int)idata);
		break;

	case 0x14:	/*  MAPLE_ENABLE  */
		if (writeflag == MEM_WRITE)
			d->enable = idata;
		else
			odata = d->enable;
		break;

	case 0x18:	/*  MAPLE_STATE  */
		if (writeflag == MEM_WRITE) {
			switch (idata) {
			case 0:	break;
			case 1:	maple_do_dma_xfer(cpu, d);
				break;
			default:fatal("[ dreamcast_maple: UNIMPLEMENTED "
				    "state value %i ]\n", (int)idata);
			}
		} else {
			/*  Always return 0 to indicate DMA xfer complete.  */
			odata = 0;
		}
		break;

	case 0x80:	/*  MAPLE_SPEED  */
		if (writeflag == MEM_WRITE) {
			d->timeout = (idata >> 16) & 0xffff;
			debug("[ dreamcast_maple: timeout set to %i ]\n",
			    d->timeout);
		} else {
			odata = d->timeout << 16;
		}
		break;

	case 0x8c:	/*  MAPLE_RESET  */
		if (writeflag == MEM_WRITE) {
			if (idata != 0x6155404f)
				fatal("[ dreamcast_maple: UNIMPLEMENTED reset "
				    "value 0x%08x ]\n", (int)idata);
			d->enable = 0;
		}
		break;

	default:if (writeflag == MEM_READ) {
			fatal("[ dreamcast_maple: UNIMPLEMENTED read from "
			    "addr 0x%x ]\n", (int)relative_addr);
		} else {
			fatal("[ dreamcast_maple: UNIMPLEMENTED write to addr "
			    "0x%x: 0x%x ]\n", (int)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(dreamcast_maple)
{
	struct machine *machine = devinit->machine;
	struct dreamcast_maple_data *d =
	    malloc(sizeof(struct dreamcast_maple_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dreamcast_maple_data));

	memory_device_register(machine->memory, devinit->name,
	    0x5f6c00, 0x100, dev_dreamcast_maple_access, d, DM_DEFAULT, NULL);

	return 1;
}


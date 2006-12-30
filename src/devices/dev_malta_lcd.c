/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_malta_lcd.c,v 1.8 2006-12-30 13:30:58 debug Exp $
 *
 *  Malta (evbmips) LCD thingy. Mostly a dummy device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define	DEV_MALTA_LCD_LENGTH		0x80
#define	MALTA_LCD_TICK_SHIFT		15
#define	LCD_LEN				8

struct malta_lcd_data {
	int		display_modified;
	unsigned char	display[LCD_LEN];
};



/*
 *  dev_malta_lcd_tick():
 */     
void dev_malta_lcd_tick(struct cpu *cpu, void *extra)
{ 
	struct malta_lcd_data *d = extra;
	int i;

	if (d->display_modified == 0)
		return;
	if (d->display_modified == 1) {
		d->display_modified = 2;
		return;
	}
	debug("[ malta_lcd:  ");
	for (i=0; i<LCD_LEN; i++)
		if (d->display[i] >= ' ')
			debug("%c", d->display[i]);
	debug("  ]\n");
	d->display_modified = 0;
}


/*
 *  dev_malta_lcd_access():
 */
DEVICE_ACCESS(malta_lcd)
{
	struct malta_lcd_data *d = (struct malta_lcd_data *) extra;
	uint64_t idata = 0, odata = 0;
	int i;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x18:
	case 0x20:
	case 0x28:
	case 0x30:
	case 0x38:
	case 0x40:
	case 0x48:
	case 0x50:
		i = (relative_addr - 0x18) / 8;
		if (writeflag == MEM_WRITE) {
			d->display[i] = idata;
			d->display_modified = 1;
		} else
			odata = d->display[i];
		break;
	default:if (writeflag == MEM_WRITE) {
			fatal("[ malta_lcd: unimplemented write to "
			    "offset 0x%x: data=0x%02x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ malta_lcd: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(malta_lcd)
{
	struct malta_lcd_data *d = malloc(sizeof(struct malta_lcd_data));

	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct malta_lcd_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_MALTA_LCD_LENGTH,
	    dev_malta_lcd_access, (void *)d, DM_DEFAULT, NULL);

	machine_add_tickfunction(devinit->machine, dev_malta_lcd_tick,
	    d, MALTA_LCD_TICK_SHIFT, 0.0);

	return 1;
}


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
 *  $Id: dev_pal.c,v 1.3 2006-03-05 16:00:22 debug Exp $
 *  
 *  PAL (TV) emulation. Experimental.
 *
 *  TODO: Almost everything.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct pal_data {
	uint64_t	addr;
	struct vfb_data	*fb;

	double		hz;
	int		cur_scanline;
	int		cur_x;

	int		visible_x, visible_y;
};


/*
 *  dev_pal_tick():
 *
 *  Called each cycle.
 */
void dev_pal_tick(struct cpu *cpu, void *extra)
{
	struct pal_data *d = extra;
	int signal = 0x80;

	switch (cpu->machine->machine_type) {

	case MACHINE_AVR_PAL:
		signal = cpu->cd.avr.portd_write * 4;
		break;

	case MACHINE_AVR_MAHPONG:
		signal = (cpu->cd.avr.portc_write & 0x80) |
		    ((cpu->cd.avr.portc_write & 0x08) << 3) |
		    ((cpu->cd.avr.portd_write & 0x10) << 1);
		break;
	}

	d->fb->framebuffer[3 * (d->cur_x + d->cur_scanline * d->visible_x)
	    + 0] = signal;
	d->fb->framebuffer[3 * (d->cur_x + d->cur_scanline * d->visible_x)
	    + 1] = signal;
	d->fb->framebuffer[3 * (d->cur_x + d->cur_scanline * d->visible_x)
	    + 2] = signal;

	d->cur_x ++;
	if (d->cur_x >= d->visible_x) {
		d->cur_x = 0;
		d->cur_scanline ++;
		if (d->cur_scanline >= d->visible_y) {
			d->cur_scanline = 0;
d->fb->update_x1 = 0;
d->fb->update_x2 = d->visible_x - 1;
d->fb->update_y1 = 0;
d->fb->update_y2 = d->visible_y - 1;
		}
	}
}


DEVINIT(pal)
{
	struct pal_data *d;
	d = malloc(sizeof(struct pal_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pal_data));

	d->hz = 12000000;
	d->addr = 0x9000000;
	d->visible_x = 579;
	d->visible_y = 625;

	switch (devinit->machine->machine_type) {
	case MACHINE_AVR_PAL:
	case MACHINE_AVR_MAHPONG:
		break;
	default:fatal("Not yet implemented for this machine type.\n");
		exit(1);
	}

	d->fb = dev_fb_init(devinit->machine, devinit->machine->memory,
	    d->addr, VFB_GENERIC, d->visible_x, d->visible_y,
	    d->visible_x, d->visible_y, 24, "PAL TV");

	machine_add_tickfunction(devinit->machine, dev_pal_tick, d,
	    0, d->hz);

	return 1;
}


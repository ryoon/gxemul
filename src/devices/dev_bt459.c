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
 *  $Id: dev_bt459.c,v 1.21 2004-07-03 15:38:05 debug Exp $
 *  
 *  Brooktree 459 vdac, used by TURBOchannel graphics cards.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "bt459.h"


#ifdef WITH_X11
#include <X11/Xlib.h>     
#include <X11/Xutil.h>

/*  TODO:  These must be moved!  */
#define N_GRAYCOLORS            16
extern XColor x11_graycolor[N_GRAYCOLORS];
#endif


/*  #define BT459_DEBUG  */
#define BT459_TICK_SHIFT	10

struct bt459_data {
	uint32_t	bt459_reg[DEV_BT459_NREGS];

	unsigned char	cur_addr_hi;
	unsigned char	cur_addr_lo;

	int		planes;
	int		type;

	int		irq_nr;
	int		interrupts_enable;
	int		interrupt_time;
	int		interrupt_time_reset_value;

	int		cursor_on;
	int		cursor_x;
	int		cursor_y;
	int		cursor_xsize;
	int		cursor_ysize;

	int		palette_sub_offset;	/*  0, 1, or 2  */

	int		video_on;

	struct vfb_data *vfb_data;

	/*
	 *  There is one pointer to the framebuffer's RGB palette,
	 *  and then a local copy of the palette.  256 * 3 bytes (r,g,b).
	 *  The reason for this is that when we need to blank the screen,
	 *  we can set the framebuffer's palette to all zeroes, but keep
	 *  our own copy intact, to be reused later again when the
	 *  screen is unblanked.
	 */
	unsigned char	*rgb_palette;		/*  256 * 3 (r,g,b)  */
	unsigned char	local_rgb_palette[256 * 3];
};


/*
 *  dev_bt459_tick():
 */
void dev_bt459_tick(struct cpu *cpu, void *extra)
{
	struct bt459_data *d = extra;

	/*
	 *  Vertical retrace interrupts. (This hack is kind of ugly.)
	 *  Once ever 'interrupt_time_reset_value', the interrupt is
	 *  asserted. It is acked either manually (by someone reading
	 *  a normal BT459 register or the Interrupt ack register),
	 *  or after another tick has passed.  (This is to prevent
	 *  lockups from unhandled interrupts.)
	 */
	if (d->type != BT459_PX && d->interrupts_enable && d->irq_nr > 0) {
		d->interrupt_time --;
		if (d->interrupt_time < 0) {
			d->interrupt_time = d->interrupt_time_reset_value;
			cpu_interrupt(cpu, d->irq_nr);
		} else
			cpu_interrupt_ack(cpu, d->irq_nr);
	}
}


/*
 *  bt459_sync_xysize():
 */
void bt459_sync_xysize(struct bt459_data *d)
{
	int x,y, xmax=0, ymax=0;

	for (y=0; y<64; y++)
		for (x=0; x<64; x+=4) {
			int reg = BT459_REG_CRAM_BASE + y*16 + x/4;
			unsigned char data = d->bt459_reg[reg];
			if (data)		ymax = y;
			if (data & 0xc0)	xmax = x;
			if (data & 0x30)	xmax = x + 1;
			if (data & 0x0c)	xmax = x + 2;
			if (data & 0x03)	xmax = x + 3;
		}

	d->cursor_xsize = xmax + 1;
	d->cursor_ysize = ymax + 1;
}


/*
 *  bt459_update_X_cursor():
 *
 *  This routine takes the color values in the cursor RAM area, and put them
 *  in the framebuffer window's cursor_ximage.
 *
 *  d->cursor_xsize and ysize are also updated.
 */
void bt459_update_X_cursor(struct bt459_data *d)
{
	struct fb_window *win = d->vfb_data->fb_window;
	int i, x,y, xmax=0, ymax=0;

	/*  First, let's calculate the size of the cursor:  */
	for (y=0; y<64; y++)
		for (x=0; x<64; x+=4) {
			int reg = BT459_REG_CRAM_BASE + y*16 + x/4;
			unsigned char data = d->bt459_reg[reg];

			if (data)
				ymax = y;

			for (i=0; i<4; i++) {
				int color = (data >> (6-2*i)) & 3;
				if (color != 0)
					xmax = x + i;
			}
		}

	d->cursor_xsize = xmax + 1;
	d->cursor_ysize = ymax + 1;

	/*  Now let's use XPutPixel to set cursor_ximage pixels:  */
	for (y=0; y<=ymax; y++)
		for (x=0; x<=xmax; x+=4) {
			int i;
			int reg = BT459_REG_CRAM_BASE + y*16 + x/4;
			unsigned char data = d->bt459_reg[reg];

			for (i=0; i<4; i++) {
				int color = (data >> (6-2*i)) & 3;

#ifdef WITH_X11
				/*
				 *  TODO:  Better (color averaging)
				 *  scaledown.
				 */
				XPutPixel(win->cursor_ximage,
				    (x + i) / win->scaledown,
				    y / win->scaledown,
				    x11_graycolor[color * 5].pixel);
#endif
			}
		}
}


/*
 *  schedule_redraw_of_whole_screen():
 */
void schedule_redraw_of_whole_screen(struct bt459_data *d)
{
	d->vfb_data->update_x1 = 0;
	d->vfb_data->update_x2 = d->vfb_data->xsize - 1;
	d->vfb_data->update_y1 = 0;
	d->vfb_data->update_y2 = d->vfb_data->ysize - 1;
}


/*
 *  dev_bt459_irq_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_bt459_irq_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct bt459_data *d = (struct bt459_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

#ifdef BT459_DEBUG
	falal("[ bt459: IRQ ack ]\n");
#endif

	d->interrupts_enable = 1;
	if (d->irq_nr > 0)
		cpu_interrupt_ack(cpu, d->irq_nr);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_bt459_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_bt459_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct bt459_data *d = (struct bt459_data *) extra;
	uint64_t idata = 0, odata = 0;
	int btaddr, new_cursor_x, new_cursor_y;
	int old_cursor_on = d->cursor_on;

	idata = memory_readmax64(cpu, data, len);

#ifdef BT459_DEBUG
	if (writeflag == MEM_WRITE)
		fatal("[ bt459: write to addr 0x%02x: %08x ]\n",
		    (int)relative_addr, (int)idata);
#endif

	/*
	 *  Vertical retrace interrupts are acked either by
	 *  accessing a normal BT459 register, or the irq register,
	 *  or by simply "missing" it.
	 */
	if (d->irq_nr > 0)
		cpu_interrupt_ack(cpu, d->irq_nr);

	/*  ID register is read-only, should always be 0x4a or 0x4a4a4a:  */
	if (d->planes == 24)
		d->bt459_reg[BT459_REG_ID] = 0x4a4a4a;
	else {
		/*
		 *  TODO:  Is it really 0x4a, or 0x4a0000?
		 *  Ultrix panics with a "bad VDAC ID" message if 0x4a is returned.
		 */
		d->bt459_reg[BT459_REG_ID] = 0x4a0000;
	}

	btaddr = ((d->cur_addr_hi << 8) + d->cur_addr_lo) % DEV_BT459_NREGS;

	/*  Read from/write to the bt459:  */
	switch (relative_addr) {
	case 0x00:		/*  Low byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to Low Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_lo = idata;
			d->palette_sub_offset = 0;
		} else {
			odata = d->cur_addr_lo;
			debug("[ bt459: read from Low Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x04:		/*  High byte of address:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to High Address Byte, 0x%02x ]\n", idata);
			d->cur_addr_hi = idata;
			d->palette_sub_offset = 0;
		} else {
			odata = d->cur_addr_hi;
			debug("[ bt459: read from High Address Byte: 0x%0x ]\n", odata);
		}
		break;
	case 0x08:		/*  Register access:  */
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: write to BT459 register 0x%04x, value 0x%02x ]\n", btaddr, idata);
			d->bt459_reg[btaddr] = idata;

			switch (btaddr) {
			case BT459_REG_PRM:
				/*
				 *  NetBSD writes 0x00 to this register to
				 *  blank the screen (video off), and 0xff
				 *  to turn the screen on.
				 */
				switch (idata & 0xff) {
				case 0:	d->video_on = 0;
					memset(d->rgb_palette, 0, 256*3);
					schedule_redraw_of_whole_screen(d);
					debug("[ bt459: video OFF ]\n");
					break;
				default:d->video_on = 1;
					memcpy(d->rgb_palette,
					    d->local_rgb_palette, 256*3);
					schedule_redraw_of_whole_screen(d);
					debug("[ bt459: video ON ]\n");
				}
				break;
			case BT459_REG_CCR:
				/*  Cursor control register:  */
				switch (idata & 0xff) {
				case 0x00:	d->cursor_on = 0; break;
				case 0xc0:
				case 0xc1:	d->cursor_on = 1; break;
				default:
					fatal("[ bt459: unimplemented CCR value 0x%08x ]\n", idata);
				}
				break;
			default:
				if (btaddr < 0x100)
					fatal("[ bt459: write to BT459 register 0x%04x, value 0x%02x ]\n", btaddr, idata);
			}

			/*  Write to cursor bitmap:  */
			if (btaddr >= BT459_REG_CRAM_BASE)
				bt459_update_X_cursor(d);	/*  or  bt459_sync_xysize(d);  */
		} else {
			odata = d->bt459_reg[btaddr];

			/*  Perhaps this hack is not neccessary:  */
			if (btaddr == BT459_REG_ID && len==1)
				odata = (odata >> 16) & 255;

			debug("[ bt459: read from BT459 register 0x%04x, value 0x%02x ]\n", btaddr, odata);
		}

		/*  Go to next register:  */
		d->cur_addr_lo ++;
		if (d->cur_addr_lo == 0)
			d->cur_addr_hi ++;
		break;
	case 0xc:		/*  Color map:  */
		if (writeflag == MEM_WRITE) {
			idata &= 255;
			debug("[ bt459: write to BT459 colormap 0x%04x subaddr %i, value 0x%02x ]\n", btaddr, d->palette_sub_offset, idata);

			if (btaddr < 0x100) {
				if (d->video_on &&
				    d->local_rgb_palette[(btaddr & 0xff) * 3
				    + d->palette_sub_offset] != idata)
					schedule_redraw_of_whole_screen(d);

				/*
				 *  Actually, the palette should only be
				 *  updated after the third write,
				 *  but this should probably work fine too:
				 */
				d->local_rgb_palette[(btaddr & 0xff) * 3
				    + d->palette_sub_offset] = idata;

				if (d->video_on)
					d->rgb_palette[(btaddr & 0xff) * 3
					    + d->palette_sub_offset] = idata;
			}
		} else {
			if (btaddr < 0x100)
				odata = d->local_rgb_palette[(btaddr & 0xff)
				    * 3 + d->palette_sub_offset];
			debug("[ bt459: read from BT459 colormap 0x%04x subaddr %i, value 0x%02x ]\n", btaddr, d->palette_sub_offset, odata);
		}

		d->palette_sub_offset ++;
		if (d->palette_sub_offset >= 3) {
			d->palette_sub_offset = 0;

			d->cur_addr_lo ++;
			if (d->cur_addr_lo == 0)
				d->cur_addr_hi ++;
		}

		break;
	default:
		if (writeflag == MEM_WRITE) {
			debug("[ bt459: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		} else {
			debug("[ bt459: unimplemented read from address 0x%x ]\n", relative_addr);
		}
	}

	/*  NetBSD uses 370,37 as magic values. (For the PX[G] cards.)  */
	new_cursor_x = (d->bt459_reg[BT459_REG_CXLO] & 255) +
	    ((d->bt459_reg[BT459_REG_CXHI] & 255) << 8) - 370;
	new_cursor_y = (d->bt459_reg[BT459_REG_CYLO] & 255) +
	    ((d->bt459_reg[BT459_REG_CYHI] & 255) << 8) - 37;

	if (d->type == BT459_BA) {
		/*  For PMAG-BA: (found by empirical testing with Ultrix)  */
		new_cursor_x += 150;
		new_cursor_y += 14;
	}

	if (new_cursor_x != d->cursor_x || new_cursor_y != d->cursor_y ||
	    d->cursor_on != old_cursor_on) {
		int ysize_mul = 1;

		d->cursor_x = new_cursor_x;
		d->cursor_y = new_cursor_y;

		/*
		 *  Ugly hack for Ultrix:
		 *
		 *  Ultrix and NetBSD assume that the cursor works differently.
		 *  Ultrix uses the 370,38 coordinates, but draws the cursor
		 *  upwards. NetBSD draws it downwards.  Ultrix also makes the
		 *  cursor smaller (?).
		 *
		 *  TODO: This actually depends on which ultrix kernel you use.
		 *  Clearly, the BT459 emulation is not implemented well
		 *  enough yet.
		 *
		 *  TODO:  Find out why? Is it because of special BT459
		 *  commands?
		 */
		if (!(d->bt459_reg[BT459_REG_CCR] & 1)) {
/*			ysize_mul = 4; */
			d->cursor_y += 5 - (d->cursor_ysize * ysize_mul);
		}

		debug("[ bt459: cursor = %03i,%03i ]\n",
		    d->cursor_x, d->cursor_y);
		dev_fb_setcursor(d->vfb_data, d->cursor_x, d->cursor_y,
		    d->cursor_on, d->cursor_xsize, d->cursor_ysize*ysize_mul);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

#ifdef BT459_DEBUG
	if (writeflag == MEM_READ)
		fatal("[ bt459: read from addr 0x%02x: %08x ]\n",
		    (int)relative_addr, (int)idata);
#endif

	return 1;
}


/*
 *  dev_bt459_init():
 */
void dev_bt459_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	uint64_t baseaddr_irq, struct vfb_data *vfb_data, int planes,
	int irq_nr, int type)
{
	struct bt459_data *d = malloc(sizeof(struct bt459_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct bt459_data));
	d->vfb_data     = vfb_data;
	d->rgb_palette  = vfb_data->rgb_palette;
	d->planes       = planes;
	d->irq_nr	= irq_nr;
	d->type		= type;
	d->cursor_x     = -1;
	d->cursor_y     = -1;
	d->cursor_xsize = d->cursor_ysize = 8;	/*  anything  */
	d->video_on     = 1;

	d->interrupt_time_reset_value = 10000;

	memory_device_register(mem, "bt459", baseaddr, DEV_BT459_LENGTH,
	    dev_bt459_access, (void *)d);

	if (baseaddr_irq != 0)
		memory_device_register(mem, "bt459_irq", baseaddr_irq, 0x10000,
		    dev_bt459_irq_access, (void *)d);

	cpu_add_tickfunction(cpu, dev_bt459_tick, d, BT459_TICK_SHIFT);
}


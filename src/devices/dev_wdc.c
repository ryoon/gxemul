/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_wdc.c,v 1.34 2005-05-15 01:55:51 debug Exp $
 *  
 *  Standard IDE controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cop0.h"
#include "cpu.h"
#include "devices.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "wdcreg.h"


#define	WDC_TICK_SHIFT		14
#define	WDC_INBUF_SIZE		(512*257)

/*  INT_DELAY=2 to be safe, 1 is faster but maybe buggy.  */
#define	INT_DELAY		1

/*  #define debug fatal  */
/*  #define DATA_DEBUG  */

struct wdc_data {
	int		irq_nr;
	int		base_drive;

	int		delayed_interrupt;

	unsigned char	identify_struct[512];

	unsigned char	inbuf[WDC_INBUF_SIZE];
	int		inbuf_head;
	int		inbuf_tail;

	int		write_in_progress;
	int		write_count;
	int64_t		write_offset;

	int		error;
	int		precomp;
	int		seccnt;
	int		sector;
	int		cyl_lo;
	int		cyl_hi;
	int		sectorsize;
	int		lba;
	int		drive;
	int		head;
	int		cur_command;
};


/*
 *  dev_wdc_tick():
 */
void dev_wdc_tick(struct cpu *cpu, void *extra)
{ 
	struct wdc_data *d = extra;

	if (d->delayed_interrupt) {
		d->delayed_interrupt --;

		if (d->delayed_interrupt == 0)
			cpu_interrupt(cpu, d->irq_nr);
	}
}


/*
 *  wdc_addtoinbuf():
 *
 *  Write to the inbuf at its head, read at its tail.
 */
static void wdc_addtoinbuf(struct wdc_data *d, int c)
{
	d->inbuf[d->inbuf_head] = c;

	d->inbuf_head = (d->inbuf_head + 1) % WDC_INBUF_SIZE;
	if (d->inbuf_head == d->inbuf_tail)
		fatal("WARNING! wdc inbuf overrun\n");
}


/*
 *  wdc_get_inbuf():
 *
 *  Read from the tail of inbuf.
 */
static uint64_t wdc_get_inbuf(struct wdc_data *d)
{
	int c = d->inbuf[d->inbuf_tail];

	if (d->inbuf_head == d->inbuf_tail) {
		fatal("WARNING! someone is reading too much from the "
		    "wdc inbuf!\n");
		return -1;
	}

	d->inbuf_tail = (d->inbuf_tail + 1) % WDC_INBUF_SIZE;
	return c;
}


/*
 *  wdc_initialize_identify_struct(d):
 */
static void wdc_initialize_identify_struct(struct cpu *cpu, struct wdc_data *d)
{
	uint64_t total_size, cyls;

	total_size = diskimage_getsize(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE);

	memset(d->identify_struct, 0, sizeof(d->identify_struct));

	cyls = total_size / (63 * 16 * 512);
	if (cyls * 63*16*512 < total_size)
		cyls ++;

	/*  Offsets are in 16-bit WORDS!  High byte, then low.  */

	/*  0: general flags  */
	d->identify_struct[2 * 0 + 0] = 0;
	d->identify_struct[2 * 0 + 1] = 1 << 6;

	/*  1: nr of cylinders  */
	d->identify_struct[2 * 1 + 0] = (cyls >> 8);
	d->identify_struct[2 * 1 + 1] = cyls & 255;

	/*  3: nr of heads  */
	d->identify_struct[2 * 3 + 0] = 0;
	d->identify_struct[2 * 3 + 1] = 16;

	/*  6: sectors per track  */
	d->identify_struct[2 * 6 + 0] = 0;
	d->identify_struct[2 * 6 + 1] = 63;

	/*  10-19: Serial number  */
	memcpy(&d->identify_struct[2 * 10], "S/N 1234-5678       ", 20);

	/*  23-26: Firmware version  */
	memcpy(&d->identify_struct[2 * 23], "VER 1.0 ", 8);

	/*  27-46: Model number  */
	memcpy(&d->identify_struct[2 * 27],
	    "Fake GXemul IDE disk                    ", 40);
	/*  TODO:  Use the diskimage's filename instead?  */

	/*  47: max sectors per multitransfer  */
	d->identify_struct[2 * 47 + 0] = 0x80;
	d->identify_struct[2 * 47 + 1] = 1;	/*  1 or 16?  */

	/*  57-58: current capacity in sectors  */
	d->identify_struct[2 * 57 + 0] = ((total_size / 512) >> 24) % 255;
	d->identify_struct[2 * 57 + 1] = ((total_size / 512) >> 16) % 255;
	d->identify_struct[2 * 58 + 0] = ((total_size / 512) >> 8) % 255;
	d->identify_struct[2 * 58 + 1] = (total_size / 512) & 255;

	/*  60-61: total nr of addresable sectors  */
	d->identify_struct[2 * 60 + 0] = ((total_size / 512) >> 24) % 255;
	d->identify_struct[2 * 60 + 1] = ((total_size / 512) >> 16) % 255;
	d->identify_struct[2 * 61 + 0] = ((total_size / 512) >> 8) % 255;
	d->identify_struct[2 * 61 + 1] = (total_size / 512) & 255;

}


/*
 *  status_byte():
 */
static int status_byte(struct wdc_data *d, struct cpu *cpu)
{
	int odata = 0;

	if (diskimage_exist(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE))
		odata |= WDCS_DRDY;
	if (d->inbuf_head != d->inbuf_tail)
		odata |= WDCS_DRQ;
	if (d->write_in_progress)
		odata |= WDCS_DRQ;
	if (d->error)
		odata |= WDCS_ERR;

#if 0
	/*
	 *  TODO:  Is this correct behaviour?
	 *
	 *  NetBSD/cobalt seems to want it, but Linux on MobilePro does not.
	 */
	if (!diskimage_exist(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE))
		odata = 0xff;
#endif

	return odata;
}


/*
 *  dev_wdc_altstatus_access():
 */
int dev_wdc_altstatus_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct wdc_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  Same as the normal status byte?  */
	odata = status_byte(d, cpu);

	if (writeflag==MEM_READ)
		debug("[ wdc: read from ALTSTATUS: 0x%02x ]\n",
		    (int)odata);
	else
		debug("[ wdc: write to ALT. CTRL: 0x%02x ]\n",
		    (int)idata);

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_wdc_access():
 */
int dev_wdc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct wdc_data *d = extra;
	uint64_t idata = 0, odata = 0;
	int i;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	case wd_data:	/*  0: data  */
		if (writeflag==MEM_READ) {
			odata = 0;

			/*  TODO: This is hardcoded for little-endian?  */

			odata += wdc_get_inbuf(d);
			if (len >= 2)
				odata += (wdc_get_inbuf(d) << 8);
			if (len == 4) {
				odata += (wdc_get_inbuf(d) << 16);
				odata += (wdc_get_inbuf(d) << 24);
			}

#ifdef DATA_DEBUG
			debug("[ wdc: read from DATA: 0x%04x ]\n", odata);
#endif

			if (d->inbuf_tail != d->inbuf_head)
				d->delayed_interrupt = INT_DELAY;

		} else {
			int inbuf_len;
#ifdef DATA_DEBUG
			debug("[ wdc: write to DATA (len=%i): 0x%08lx ]\n",
			    (int)len, (long)idata);
#endif
			if (!d->write_in_progress) {
				fatal("[ wdc: write to DATA, but not "
				    "expecting any? (len=%i): 0x%08lx ]\n",
				    (int)len, (long)idata);
			}

			switch (len) {
			case 4:	wdc_addtoinbuf(d, idata & 0xff);
				wdc_addtoinbuf(d, (idata >> 8) & 0xff);
				wdc_addtoinbuf(d, (idata >> 16) & 0xff);
				wdc_addtoinbuf(d, (idata >> 24) & 0xff);
				break;
			case 2:	wdc_addtoinbuf(d, idata & 0xff);
				wdc_addtoinbuf(d, (idata >> 8) & 0xff);
				break;
			case 1:	wdc_addtoinbuf(d, idata); break;
			default:fatal("wdc: unimplemented write len %i\n", len);
				exit(1);
			}

			inbuf_len = d->inbuf_head - d->inbuf_tail;
			while (inbuf_len < 0)
				inbuf_len += WDC_INBUF_SIZE;

#if 0
			if ((inbuf_len % (512 * d->write_count)) == 0) {
#endif
			if ((inbuf_len % 512) == 0) {
				int count = 1;	/*  d->write_count;  */
				unsigned char *buf = malloc(count * 512);
				if (buf == NULL) {
					fprintf(stderr, "out of memory\n");
					exit(1);
				}

				for (i=0; i<512 * count; i++)
					buf[i] = wdc_get_inbuf(d);

				diskimage_access(cpu->machine,
				    d->drive + d->base_drive, DISKIMAGE_IDE, 1,
				    d->write_offset, buf, 512 * count);
				free(buf);

				d->write_count --;
				d->write_offset += 512;

				d->delayed_interrupt = INT_DELAY;

				if (d->write_count == 0)
					d->write_in_progress = 0;
			}
		}
		break;

	case wd_error:	/*  1: error (r), precomp (w)  */
		if (writeflag==MEM_READ) {
			odata = d->error;
			debug("[ wdc: read from ERROR: 0x%02x ]\n", odata);
			/*  TODO:  is the error value cleared on read?  */
			d->error = 0;
		} else {
			d->precomp = idata;
			debug("[ wdc: write to PRECOMP: 0x%02x ]\n", idata);
		}
		break;

	case wd_seccnt:	/*  2: sector count  */
		if (writeflag==MEM_READ) {
			odata = d->seccnt;
			debug("[ wdc: read from SECCNT: 0x%02x ]\n", odata);
		} else {
			d->seccnt = idata;
			debug("[ wdc: write to SECCNT: 0x%02x ]\n", idata);
		}
		break;

	case wd_sector:	/*  3: first sector  */
		if (writeflag==MEM_READ) {
			odata = d->sector;
			debug("[ wdc: read from SECTOR: 0x%02x ]\n", odata);
		} else {
			d->sector = idata;
			debug("[ wdc: write to SECTOR: 0x%02x ]\n", idata);
		}
		break;

	case wd_cyl_lo:	/*  4: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_lo;
			debug("[ wdc: read from CYL_LO: 0x%02x ]\n", odata);
		} else {
			d->cyl_lo = idata;
			debug("[ wdc: write to CYL_LO: 0x%02x ]\n", idata);
		}
		break;

	case wd_cyl_hi:	/*  5: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_hi;
			debug("[ wdc: read from CYL_HI: 0x%02x ]\n", odata);
		} else {
			d->cyl_hi = idata;
			debug("[ wdc: write to CYL_HI: 0x%02x ]\n", idata);
		}
		break;

	case wd_sdh:	/*  6: sectorsize/drive/head  */
		if (writeflag==MEM_READ) {
			odata = (d->sectorsize << 6) + (d->lba << 5) +
			    (d->drive << 4) + (d->head);
			debug("[ wdc: read from SDH: 0x%02x (sectorsize %i,"
			    " lba=%i, drive %i, head %i) ]\n", (int)odata,
			    d->sectorsize, d->lba, d->drive, d->head);
		} else {
			d->sectorsize = (idata >> 6) & 3;
			d->lba   = (idata >> 5) & 1;
			d->drive = (idata >> 4) & 1;
			d->head  = idata & 0xf;
			debug("[ wdc: write to SDH: 0x%02x (sectorsize %i,"
			    " lba=%i, drive %i, head %i) ]\n", (int)idata,
			    d->sectorsize, d->lba, d->drive, d->head);
		}
		break;

	case wd_command:	/*  7: command or status  */
		if (writeflag==MEM_READ) {
			odata = status_byte(d, cpu);
#if 1
			debug("[ wdc: read from STATUS: 0x%02x ]\n", odata);
#endif

			cpu_interrupt_ack(cpu, d->irq_nr);
			d->delayed_interrupt = 0;
		} else {
			debug("[ wdc: write to COMMAND: 0x%02x ]\n", idata);
			d->cur_command = idata;

			/*  TODO:  Is this correct behaviour?  */
			if (!diskimage_exist(cpu->machine,
			    d->drive + d->base_drive, DISKIMAGE_IDE)) {
				d->error |= WDCE_ABRT;
				d->delayed_interrupt = INT_DELAY;
				break;
			}

			/*  Handle the command:  */
			switch (d->cur_command) {
			case WDCC_READ:
				debug("[ wdc: READ from drive %i, head %i, "
				    "cylinder %i, sector %i, nsecs %i ]\n",
				    d->drive, d->head, d->cyl_hi*256+d->cyl_lo,
				    d->sector, d->seccnt);
				/*  TODO:  HAHA! This should be removed
				    quickly  */
				{
					unsigned char buf[512*256];
					int cyl = d->cyl_hi * 256+ d->cyl_lo;
					int count = d->seccnt? d->seccnt : 256;
					uint64_t offset = 512 * (d->sector - 1
					    + d->head * 63 + 16*63*cyl);

#if 0
/*  LBA:  */
if (d->lba)
	offset = 512 * (((d->head & 0xf) << 24) + (cyl << 8) + d->sector);
printf("WDC read from offset %lli\n", (long long)offset);
#endif
					diskimage_access(cpu->machine,
					    d->drive + d->base_drive,
					    DISKIMAGE_IDE, 0,
					    offset, buf, 512 * count);
					/*  TODO: result code  */
					for (i=0; i<512 * count; i++)
						wdc_addtoinbuf(d, buf[i]);
				}
				d->delayed_interrupt = INT_DELAY;
				break;
			case WDCC_WRITE:
				debug("[ wdc: WRITE to drive %i, head %i, "
				    "cylinder %i, sector %i, nsecs %i ]\n",
				    d->drive, d->head, d->cyl_hi*256+d->cyl_lo,
				    d->sector, d->seccnt);
				/*  TODO:  HAHA! This should be removed
				    quickly  */
				{
					int cyl = d->cyl_hi * 256+ d->cyl_lo;
					int count = d->seccnt? d->seccnt : 256;
					uint64_t offset = 512 * (d->sector - 1
					    + d->head * 63 + 16*63*cyl);

#if 0
/*  LBA:  */
if (d->lba)
	offset = 512 * (((d->head & 0xf) << 24) + (cyl << 8) + d->sector);
printf("WDC write to offset %lli\n", (long long)offset);
#endif

					d->write_in_progress = 1;
					d->write_count = count;
					d->write_offset = offset;

					/*  TODO: result code  */
				}
/*  TODO: Really interrupt here?  */
#if 0
				d->delayed_interrupt = INT_DELAY;
#endif
				break;

			case WDCC_IDP:	/*  Initialize drive parameters  */
				debug("[ wdc: IDP drive %i (TODO) ]\n",
				    d->drive);
				/*  TODO  */
				d->delayed_interrupt = INT_DELAY;
				break;
			case SET_FEATURES:
				fatal("[ wdc: SET_FEATURES drive %i (TODO), "
				    "feature 0x%02x ]\n", d->drive, d->precomp);
				/*  TODO  */
				switch (d->precomp) {
				case WDSF_SET_MODE:
					fatal("[ wdc: WDSF_SET_MODE drive %i, "
					    "pio/dma flags 0x%02x ]\n",
					    d->drive, d->seccnt);
					break;
				default:
					d->error |= WDCE_ABRT;
				}
				/*  TODO: always interrupt?  */
				d->delayed_interrupt = INT_DELAY;
				break;
			case WDCC_RECAL:
				debug("[ wdc: RECAL drive %i ]\n", d->drive);
				d->delayed_interrupt = INT_DELAY;
				break;
			case WDCC_IDENTIFY:
				debug("[ wdc: IDENTIFY drive %i ]\n", d->drive);
				wdc_initialize_identify_struct(cpu, d);
				/*  The IDENTIFY data block is sent out
				    in low/high byte order:  */
				for (i=0; i<sizeof(d->identify_struct); i+=2) {
					wdc_addtoinbuf(d, d->identify_struct
					    [i+1]);
					wdc_addtoinbuf(d, d->identify_struct
					    [i+0]);
				}
				d->delayed_interrupt = INT_DELAY;
				break;
			case WDCC_IDLE_IMMED:
				debug("[ wdc: IDLE_IMMED drive %i ]\n",
				    d->drive);
				/*  TODO: interrupt here?  */
				d->delayed_interrupt = INT_DELAY;
				break;
			default:
				/*  TODO  */
				d->error |= WDCE_ABRT;

				fatal("[ wdc: unknown command 0x%02x ("
				    "drive %i, head %i, cylinder %i, sector %i,"
				    " nsecs %i) ]\n", d->cur_command, d->drive,
				    d->head, d->cyl_hi*256+d->cyl_lo,
				    d->sector, d->seccnt);
				exit(1);
			}
		}
		break;

	default:
		if (writeflag==MEM_READ)
			debug("[ wdc: read from 0x%02x ]\n",
			    (int)relative_addr);
		else
			debug("[ wdc: write to  0x%02x: 0x%02x ]\n",
			    (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_wdc_init():
 *
 *  base_drive should be 0 for the primary device, and 2 for the secondary.
 */
void dev_wdc_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr, int irq_nr, int base_drive)
{
	struct wdc_data *d;
	uint64_t alt_status_addr;

	d = malloc(sizeof(struct wdc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct wdc_data));
	d->irq_nr     = irq_nr;
	d->base_drive = base_drive;

	alt_status_addr = baseaddr + 0x206;

	/*  Special hack for pcic/hpcmips:  TODO: Fix  */
	if (baseaddr == 0x14000180)
		alt_status_addr = 0x14000386;

	memory_device_register(mem, "wdc_altstatus", alt_status_addr, 2,
	    dev_wdc_altstatus_access, d, MEM_DEFAULT, NULL);
	memory_device_register(mem, "wdc", baseaddr, DEV_WDC_LENGTH,
	    dev_wdc_access, d, MEM_DEFAULT, NULL);

	machine_add_tickfunction(machine, dev_wdc_tick,
	    d, WDC_TICK_SHIFT);
}


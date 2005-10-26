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
 *  $Id: dev_wdc.c,v 1.44 2005-10-26 14:37:05 debug Exp $
 *
 *  Standard "wdc" IDE controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "diskimage.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "wdcreg.h"

#define	DEV_WDC_LENGTH		8
#define	WDC_TICK_SHIFT		14
#define	WDC_MAX_SECTORS		512
#define	WDC_INBUF_SIZE		(512*(WDC_MAX_SECTORS+1))

/*
 *  INT_DELAY: This is an old hack which only exists because (some versions of)
 *  NetBSD for hpcmips have interrupt problems. These problems are probably not
 *  specific to GXemul, but are also triggered on real hardware.
 *
 *  See the following URL for more info:
 *  http://mail-index.netbsd.org/port-hpcmips/2004/12/30/0003.html
 */
#define	INT_DELAY		1

extern int quiet_mode;

/*  #define debug fatal  */

struct wdc_data {
	int		irq_nr;
	int		base_drive;
	int		data_debug;

	/*  Cached values:  */
	int		cyls[2];
	int		heads[2];
	int		sectors_per_track[2];

	unsigned char	identify_struct[512];

	unsigned char	inbuf[WDC_INBUF_SIZE];
	int		inbuf_head;
	int		inbuf_tail;

	int		delayed_interrupt;
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
		fatal("[ wdc_addtoinbuf(): WARNING! wdc inbuf overrun!"
		    " Increase WDC_MAX_SECTORS. ]\n");
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
		fatal("[ wdc: WARNING! someone is reading too much from the "
		    "wdc inbuf! ]\n");
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
	uint64_t total_size;
	char namebuf[40];

	total_size = diskimage_getsize(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE);

	memset(d->identify_struct, 0, sizeof(d->identify_struct));

	/*  Offsets are in 16-bit WORDS!  High byte, then low.  */

	/*  0: general flags  */
	d->identify_struct[2 * 0 + 0] = 0;
	d->identify_struct[2 * 0 + 1] = 1 << 6;

	/*  1: nr of cylinders  */
	d->identify_struct[2 * 1 + 0] = (d->cyls[d->drive] >> 8);
	d->identify_struct[2 * 1 + 1] = d->cyls[d->drive] & 255;

	/*  3: nr of heads  */
	d->identify_struct[2 * 3 + 0] = d->heads[d->drive] >> 8;
	d->identify_struct[2 * 3 + 1] = d->heads[d->drive];

	/*  6: sectors per track  */
	d->identify_struct[2 * 6 + 0] = d->sectors_per_track[d->drive] >> 8;
	d->identify_struct[2 * 6 + 1] = d->sectors_per_track[d->drive];

	/*  10-19: Serial number  */
	memcpy(&d->identify_struct[2 * 10], "S/N 1234-5678       ", 20);

	/*  23-26: Firmware version  */
	memcpy(&d->identify_struct[2 * 23], "VER 1.0 ", 8);

	/*  27-46: Model number  */
	if (diskimage_getname(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE, namebuf, sizeof(namebuf))) {
		int i;
		for (i=0; i<sizeof(namebuf); i++)
			if (namebuf[i] == 0) {
				for (; i<sizeof(namebuf); i++)
					namebuf[i] = ' ';
				break;
			}
		memcpy(&d->identify_struct[2 * 27], namebuf, 40);
	} else
		memcpy(&d->identify_struct[2 * 27],
		    "Fake GXemul IDE disk                    ", 40);

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
 *  wdc__read():
 */
void wdc__read(struct cpu *cpu, struct wdc_data *d)
{
	const int max_sectors_per_chunk = 64;
	unsigned char buf[512 * max_sectors_per_chunk];
	int i, cyl = d->cyl_hi * 256+ d->cyl_lo;
	int count = d->seccnt? d->seccnt : 256;
	uint64_t offset = 512 * (d->sector - 1
	    + d->head * d->sectors_per_track[d->drive] +
	    d->heads[d->drive] * d->sectors_per_track[d->drive] * cyl);

#if 0
	/*  LBA:  */
	if (d->lba)
		offset = 512 * (((d->head & 0xf) << 24) + (cyl << 8)
		    + d->sector);
	printf("WDC read from offset %lli\n", (long long)offset);
#endif

	while (count > 0) {
		int to_read = count > max_sectors_per_chunk?
		    max_sectors_per_chunk : count;

		/*  TODO: result code from the read?  */

		if (d->inbuf_head + 512 * to_read <= WDC_INBUF_SIZE) {
			diskimage_access(cpu->machine, d->drive + d->base_drive,
			    DISKIMAGE_IDE, 0, offset,
			    d->inbuf + d->inbuf_head, 512 * to_read);
			d->inbuf_head += 512 * to_read;
			if (d->inbuf_head == WDC_INBUF_SIZE)
				d->inbuf_head = 0;
		} else {
			diskimage_access(cpu->machine, d->drive + d->base_drive,
			    DISKIMAGE_IDE, 0, offset, buf, 512 * to_read);
			for (i=0; i<512 * to_read; i++)
				wdc_addtoinbuf(d, buf[i]);
		}

		offset += 512 * to_read;
		count -= to_read;
	}

	d->delayed_interrupt = INT_DELAY;
}


/*
 *  wdc__write():
 */
void wdc__write(struct cpu *cpu, struct wdc_data *d)
{
	int cyl = d->cyl_hi * 256+ d->cyl_lo;
	int count = d->seccnt? d->seccnt : 256;
	uint64_t offset = 512 * (d->sector - 1
	    + d->head * d->sectors_per_track[d->drive] +
	    d->heads[d->drive] * d->sectors_per_track[d->drive] * cyl);
#if 0
	/*  LBA:  */
	if (d->lba)
		offset = 512 * (((d->head & 0xf) << 24) +
		    (cyl << 8) + d->sector);
	printf("WDC write to offset %lli\n", (long long)offset);
#endif

	d->write_in_progress = 1;
	d->write_count = count;
	d->write_offset = offset;

	/*  TODO: result code?  */
}


/*
 *  status_byte():
 *
 *  Return a reasonable status byte corresponding to the controller's current
 *  state.
 */
static int status_byte(struct wdc_data *d, struct cpu *cpu)
{
	int odata = 0;

	/*
	 *  Modern versions of OpenBSD wants WDCS_DSC. (Thanks to Alexander
	 *  Yurchenko for noticing this.)
	 */
	if (diskimage_exist(cpu->machine, d->drive + d->base_drive,
	    DISKIMAGE_IDE))
		odata |= WDCS_DRDY | WDCS_DSC;
	if (d->inbuf_head != d->inbuf_tail)
		odata |= WDCS_DRQ;
	if (d->write_in_progress)
		odata |= WDCS_DRQ;
	if (d->error)
		odata |= WDCS_ERR;

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

	idata = data[0];

	/*  Same as the normal status byte?  */
	odata = status_byte(d, cpu);

	if (writeflag==MEM_READ)
		debug("[ wdc: read from ALTSTATUS: 0x%02x ]\n",
		    (int)odata);
	else
		debug("[ wdc: write to ALT. CTRL: 0x%02x ]\n",
		    (int)idata);

	if (writeflag == MEM_READ)
		data[0] = odata;

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

	if (writeflag == MEM_WRITE) {
		if (relative_addr == wd_data)
			idata = memory_readmax64(cpu, data, len);
		else
			idata = data[0];
	}

	switch (relative_addr) {

	case wd_data:	/*  0: data  */
		if (writeflag == MEM_READ) {
			odata = 0;

			/*  TODO: This is hardcoded for little-endian?  */

			odata += wdc_get_inbuf(d);
			if (len >= 2)
				odata += (wdc_get_inbuf(d) << 8);
			if (len == 4) {
				odata += (wdc_get_inbuf(d) << 16);
				odata += (wdc_get_inbuf(d) << 24);
			}

			if (d->data_debug)
				debug("[ wdc: read from DATA: 0x%04x ]\n",
				    (int)odata);
#if 0
			if (d->inbuf_tail != d->inbuf_head)
#else
			if (d->inbuf_tail != d->inbuf_head &&
			    ((d->inbuf_tail - d->inbuf_head) % 512) == 0)
#endif
				d->delayed_interrupt = INT_DELAY;
		} else {
			int inbuf_len;
			if (d->data_debug)
				debug("[ wdc: write to DATA (len=%i): "
				    "0x%08lx ]\n", (int)len, (long)idata);
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
#else
			if ((inbuf_len % 512) == 0) {
#endif
				int count = 1;	/*  d->write_count;  */
				unsigned char buf[512 * count];
				unsigned char *b = buf;

				if (d->inbuf_tail+512*count <= WDC_INBUF_SIZE) {
					b = d->inbuf + d->inbuf_tail;
					d->inbuf_tail = (d->inbuf_tail + 512
					    * count) % WDC_INBUF_SIZE;
				} else {
					for (i=0; i<512 * count; i++)
						buf[i] = wdc_get_inbuf(d);
				}

				diskimage_access(cpu->machine,
				    d->drive + d->base_drive, DISKIMAGE_IDE, 1,
				    d->write_offset, b, 512 * count);

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
			debug("[ wdc: read from ERROR: 0x%02x ]\n",
			    (int)odata);
			/*  TODO:  is the error value cleared on read?  */
			d->error = 0;
		} else {
			d->precomp = idata;
			debug("[ wdc: write to PRECOMP: 0x%02x ]\n",(int)idata);
		}
		break;

	case wd_seccnt:	/*  2: sector count  */
		if (writeflag==MEM_READ) {
			odata = d->seccnt;
			debug("[ wdc: read from SECCNT: 0x%02x ]\n",(int)odata);
		} else {
			d->seccnt = idata;
			debug("[ wdc: write to SECCNT: 0x%02x ]\n", (int)idata);
		}
		break;

	case wd_sector:	/*  3: first sector  */
		if (writeflag==MEM_READ) {
			odata = d->sector;
			debug("[ wdc: read from SECTOR: 0x%02x ]\n",(int)odata);
		} else {
			d->sector = idata;
			debug("[ wdc: write to SECTOR: 0x%02x ]\n", (int)idata);
		}
		break;

	case wd_cyl_lo:	/*  4: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_lo;
			debug("[ wdc: read from CYL_LO: 0x%02x ]\n",(int)odata);
		} else {
			d->cyl_lo = idata;
			debug("[ wdc: write to CYL_LO: 0x%02x ]\n", (int)idata);
		}
		break;

	case wd_cyl_hi:	/*  5: cylinder low  */
		if (writeflag==MEM_READ) {
			odata = d->cyl_hi;
			debug("[ wdc: read from CYL_HI: 0x%02x ]\n",(int)odata);
		} else {
			d->cyl_hi = idata;
			debug("[ wdc: write to CYL_HI: 0x%02x ]\n", (int)idata);
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
			if (!quiet_mode)
				debug("[ wdc: read from STATUS: 0x%02x ]\n",
				    (int)odata);
			cpu_interrupt_ack(cpu, d->irq_nr);
			d->delayed_interrupt = 0;
		} else {
			debug("[ wdc: write to COMMAND: 0x%02x ]\n",(int)idata);
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
				if (!quiet_mode)
					debug("[ wdc: READ from drive %i, head"
					    " %i, cyl %i, sector %i, nsecs %i "
					    "]\n", d->drive, d->head,
					    d->cyl_hi*256+d->cyl_lo, d->sector,
					    d->seccnt);
				wdc__read(cpu, d);
				break;

			case WDCC_WRITE:
				if (!quiet_mode)
					debug("[ wdc: WRITE to drive %i, head"
					    " %i, cyl %i, sector %i, nsecs %i"
					    " ]\n", d->drive, d->head,
					    d->cyl_hi*256+d->cyl_lo, d->sector,
					    d->seccnt);
				wdc__write(cpu, d);
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

			/*  Unsupported commands, without warning:  */
			case ATAPI_IDENTIFY_DEVICE:
			case WDCC_SEC_SET_PASSWORD:
			case WDCC_SEC_UNLOCK:
			case WDCC_SEC_ERASE_PREPARE:
			case WDCC_SEC_ERASE_UNIT:
			case WDCC_SEC_FREEZE_LOCK:
			case WDCC_SEC_DISABLE_PASSWORD:
				d->error |= WDCE_ABRT;
				break;

			default:
				/*  TODO  */
				d->error |= WDCE_ABRT;

				fatal("[ wdc: WARNING! Unimplemented command "
				    "0x%02x (drive %i, head %i, cyl %i, sector"
				    " %i, nsecs %i) ]\n", d->cur_command,
				    d->drive, d->head, d->cyl_hi*256+d->cyl_lo,
				    d->sector, d->seccnt);
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

	if (cpu->machine->machine_type != MACHINE_HPCMIPS)
		dev_wdc_tick(cpu, extra);

	if (writeflag == MEM_READ) {
		if (relative_addr == wd_data)
			memory_writemax64(cpu, data, len, odata);
		else
			data[0] = odata;
	}

	return 1;
}


/*
 *  devinit_wdc():
 */
int devinit_wdc(struct devinit *devinit)
{
	struct wdc_data *d;
	uint64_t alt_status_addr;
	int i, tick_shift = WDC_TICK_SHIFT;

	d = malloc(sizeof(struct wdc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct wdc_data));
	d->irq_nr = devinit->irq_nr;

	/*  base_drive = 0 for the primary controller, 2 for the secondary.  */
	d->base_drive = 0;
	if ((devinit->addr & 0xfff) == 0x170)
		d->base_drive = 2;

	alt_status_addr = devinit->addr + 0x206;

	/*  Special hack for pcic/hpcmips:  TODO: Fix  */
	if (devinit->addr == 0x14000180)
		alt_status_addr = 0x14000386;

	/*  Get disk geometries:  */
	for (i=0; i<2; i++)
		if (diskimage_exist(devinit->machine, d->base_drive +i,
		    DISKIMAGE_IDE))
			diskimage_getchs(devinit->machine, d->base_drive + i,
			    DISKIMAGE_IDE, &d->cyls[i], &d->heads[i],
			    &d->sectors_per_track[i]);

	memory_device_register(devinit->machine->memory, "wdc_altstatus",
	    alt_status_addr, 2, dev_wdc_altstatus_access, d, MEM_DEFAULT, NULL);
	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_WDC_LENGTH, dev_wdc_access, d, MEM_DEFAULT,
	    NULL);

	if (devinit->machine->machine_type != MACHINE_HPCMIPS)
		tick_shift += 2;

	machine_add_tickfunction(devinit->machine, dev_wdc_tick,
	    d, tick_shift);

	return 1;
}


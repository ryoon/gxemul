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
 *  $Id: dev_turbochannel.c,v 1.14 2004-05-07 00:41:51 debug Exp $
 *  
 *  Generic framework for TURBOchannel devices, used in DECstation machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


struct turbochannel_data {
	int		slot_nr;
	uint64_t	baseaddr;
	uint64_t	endaddr;
	int		irq;

	char		device_name[9];		/*  NUL-terminated  */

	/*  These should be terminated with spaces  */
	char		card_firmware_version[8];
	char		card_vendor_name[8];
	char		card_module_name[8];
	char		card_firmware_type[4];
};


/*
 *  dev_turbochannel_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_turbochannel_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct turbochannel_data *d = extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ) {
		debug("[ turbochannel: read from slot %i addr 0x%08lx (", d->slot_nr, (long)relative_addr);

		switch (relative_addr) {
		case 0x3e0:  odata = 0x00000001; debug("ROM width"); break;
		case 0x3e4:  odata = 0x00000004; debug("ROM stride"); break;
		case 0x3e8:  odata = 0x00000001; debug("ROM size"); break;	/*  8KB * romsize  */
		case 0x3ec:  odata = 0x00000001; debug("slot size"); break;	/*  4MB * slotsize  */

		case 0x3f0:  odata = 0x55555555; debug("ROM signature byte 0"); break;
		case 0x3f4:  odata = 0x00000000; debug("ROM signature byte 1"); break;
		case 0x3f8:  odata = 0xaaaaaaaa; debug("ROM signature byte 2"); break;
		case 0x3fc:  odata = 0xffffffff; debug("ROM signature byte 3"); break;

		case 0x470:  odata = 0x00000000; debug("flags"); break;		/*  0=nothing, 1=parity  */

		default:
			if (relative_addr >= 0x400 && relative_addr < 0x420)
				odata = d->card_firmware_version[(relative_addr-0x400)/4];
			else if (relative_addr >= 0x420 && relative_addr < 0x440)
				odata = d->card_vendor_name[(relative_addr-0x420)/4];
			else if (relative_addr >= 0x440 && relative_addr < 0x460)
				odata = d->card_module_name[(relative_addr-0x440)/4];
			else if (relative_addr >= 0x460 && relative_addr < 0x470)
				odata = d->card_firmware_type[(relative_addr-0x460)/4];
			else {
				debug("?");
			}
		}

		/*  If this slot is empty, return error:  */
		if (d->card_module_name[0] ==  ' ')
			return 0;

		debug(") ]\n");
	} else {
		/*  debug("[ turbochannel: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_turbochannel_init():
 *
 *  This is a generic turbochannel card device.  device_name should point
 *  to a string such as "PMAG-BA".
 */
void dev_turbochannel_init(struct cpu *cpu, struct memory *mem, int slot_nr, uint64_t baseaddr, uint64_t endaddr, char *device_name, int irq)
{
	struct vfb_data *fb;
	struct turbochannel_data *d;
	int rom_offset = 0x3c0000;
	int rom_length = DEV_TURBOCHANNEL_LEN;

	if (device_name==NULL)
		return;

	if (strlen(device_name)>8) {
		fprintf(stderr, "dev_turbochannel_init(): bad device_name\n");
		exit(1);
	}

	d = malloc(sizeof(struct turbochannel_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct turbochannel_data));
	d->slot_nr  = slot_nr;
	d->baseaddr = baseaddr;
	d->endaddr  = endaddr;
	d->irq      = irq;

	strcpy(d->device_name, device_name);

	strncpy(d->card_firmware_version, "V5.3a   ", 8);
	strncpy(d->card_vendor_name,      "DEC     ", 8);
	strncpy(d->card_firmware_type,    "TCF0", 4);

	memset(d->card_module_name, ' ', 8);
	strncpy(d->card_module_name, device_name, strlen(device_name));

	/*
	 *  According to NetBSD/pmax:
	 *
	 *  PMAD-AA:  le1 at tc0 slot 2 offset 0x0: address 00:00:00:00:00:00  (ethernet)
	 *  PMAG-AA:  mfb0 at tc0 slot 2 offset 0x0: 1280x1024x8
	 *  PMAG-BA:  cfb0 at tc0 slot 2 offset 0x0cfb0: 1024x864x8            (vdac init failed)
	 *  PMAG-CA:  px0 at tc0 slot 2 offset 0x0: 2D, 4x1 stamp, 8 plane     (PMAG-DA,EA,FA,FB are also pixelstamps)
	 *  PMAG-DV:  xcfb0 at tc0 slot 2 offset 0x0: 1024x768x8
	 *  PMAG-JA:  "truecolor" in Ultrix
	 *  PMAGB-VA: sfb0 at tc0 slot 2 offset 0x0: 0x0x8
	 *  PMAZ-AA:  asc0 at tc0 slot 2 offset 0x0: NCR53C94, 25MHz, SCSI ID 7
	 */

	if (strcmp(device_name, "PMAD-AA")==0) {
		/*  le in NetBSD, Lance ethernet  */
		dev_le_init(mem, baseaddr, 0, 0, irq, 4*1048576);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAZ-AA")==0) {
		/*  asc in NetBSD, SCSI  */
		dev_asc_init(cpu, mem, baseaddr, irq);
		rom_offset = 0xc0000;
	} else if (strcmp(device_name, "PMAG-AA")==0) {
		/*  mfb in NetBSD  */
		fb = dev_fb_init(cpu, mem, baseaddr + VFB_MFB_VRAM, VFB_GENERIC, 1280, 1024, 2048, 1024, 8, "PMAG-AA");
		dev_bt455_init(mem, baseaddr + VFB_MFB_BT455, fb);	/*  palette  */
		dev_bt431_init(mem, baseaddr + VFB_MFB_BT431, fb, 8);	/*  cursor  */
		rom_offset = 0;
	} else if (strcmp(device_name, "PMAG-BA")==0) {
		/*  cfb in NetBSD  */
		fb = dev_fb_init(cpu, mem, baseaddr, VFB_GENERIC, 1024,864, 1024,1024,8, device_name);
		dev_bt459_init(cpu, mem, baseaddr + VFB_CFB_BT459, fb, 8, irq);
		rom_offset = 0x3c0000;	/*  should be 380000, but something needs to be at 0x3c0000?  */
	} else if (strcmp(device_name, "PMAG-CA")==0) {
		/*  px in NetBSD  */
		dev_px_init(cpu, mem, baseaddr, DEV_PX_TYPE_PX, irq);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-DA")==0) {
		/*  pxg in NetBSD  */
		dev_px_init(cpu, mem, baseaddr, DEV_PX_TYPE_PXG, irq);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-EA")==0) {
		/*  pxg+ in NetBSD: TODO  (not supported by the kernel I've tried)  */
		fatal("TODO (see dev_turbochannel.c)\n");
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-FA")==0) {
		/*  "pxg+ Turbo" in NetBSD  */
		dev_px_init(cpu, mem, baseaddr, DEV_PX_TYPE_PXGPLUSTURBO, irq);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-DV")==0) {
		/*  xcfb in NetBSD: TODO  */
		fb = dev_fb_init(cpu, mem, baseaddr + 0x2000000, VFB_DEC_MAXINE, 0, 0, 0, 0, 0, "PMAG-DV");
		/*  TODO:  not yet usable, needs a IMS332 vdac  */
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-JA")==0) {
		/*  "Truecolor", mixed 8- and 24-bit  */
		dev_pmagja_init(cpu, mem, baseaddr, irq);
		rom_offset = 0;		/*  NOTE: 0, not 0x3c0000  */
	} else if (strcmp(device_name, "PMAG-RO")==0) {
		/*  This works at least B/W in Ultrix, so far.  */
		/*  TODO: bt463 at offset 0x040000, bt431 at offset 0x040010 (?)  */
		fb = dev_fb_init(cpu, mem, baseaddr + 0x200000, VFB_GENERIC, 1280,1024, 1280,1024, 8, "PMAG-RO");
		dev_bt431_init(mem, baseaddr + 0x40010, fb, 8);		/*  cursor  */
		rom_offset = 0x3c0000;
	} else if (device_name[0] == '\0') {
		/*  If this slot is empty, then occupy the entire 4MB slot address range:  */
		rom_offset = 0;
		rom_length = 4*1048576;
	} else
		fatal("warning: unknown TURBOchannel device name \"%s\"\n", device_name);

	memory_device_register(mem, "turbochannel", baseaddr + rom_offset, rom_length, dev_turbochannel_access, d);
}


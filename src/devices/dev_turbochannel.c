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
 *  $Id: dev_turbochannel.c,v 1.3 2003-11-07 08:48:15 debug Exp $
 *  
 *  Generic framework for TURBOchannel devices, used in DECstation machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	int i;
	int idata = 0, odata=0, odata_set=0;

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

	odata_set = 1;

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

		debug(") ]\n");
	} else {
		/*  debug("[ turbochannel: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
	}

	return 1;
}


/*
 *  dev_turbochannel_init():
 */
void dev_turbochannel_init(struct cpu *cpu, struct memory *mem, int slot_nr, uint64_t baseaddr, uint64_t endaddr, char *device_name, int irq)
{
	struct vfb_data *fb;
	struct turbochannel_data *d;
	int rom_offset = 0x3c0000;

	if (device_name==NULL || device_name[0]=='\0')
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
	 *  PMAG-CA:  px0 at tc0 slot 2 offset 0x0: 2D, 4x1 stamp, 8 plane     (PMAG-DA,FA,FB are the same)
	 *  PMAG-DV:  xcfb0 at tc0 slot 2 offset 0x0: 1024x768x8
	 *  PMAGB-VA: sfb0 at tc0 slot 2 offset 0x0: 0x0x8
	 *  PMAZ-AA:  asc0 at tc0 slot 2 offset 0x0: NCR53C94, 25MHz, SCSI ID 7
	 */

	if (strcmp(device_name, "PMAD-AA")==0) {
		/*  le in NetBSD, Lance ethernet  */
		dev_le_init(mem, baseaddr, 0, 0, irq);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAZ-AA")==0) {
		/*  asc in NetBSD, SCSI  */
		dev_asc_init(cpu, mem, baseaddr, irq);
		rom_offset = 0x3c0000;
	} else if (strcmp(device_name, "PMAG-AA")==0) {
		/*  mfb in NetBSD  */
		fb = dev_fb_init(cpu, mem, baseaddr + VFB_MFB_VRAM, VFB_GENERIC, 1280, 1024, 2048, 1024, 8, "PMAG-AA");
		dev_bt459_init(mem, baseaddr + VFB_MFB_BT459, fb->rgb_palette, 8);
		/*  TODO: There should be a BT431 at 0x180000, and a BT455 at 0x100000. No BT459. */
		rom_offset = 0;
	} else if (strcmp(device_name, "PMAG-BA")==0) {
		/*  cfb in NetBSD  */
		fb = dev_fb_init(cpu, mem, baseaddr, VFB_GENERIC, 1024,864, 1024,1024,8, "PMAG-BA");
		dev_bt459_init(mem, baseaddr + VFB_CFB_BT459, fb->rgb_palette, 8);
		rom_offset = 0x3c0000;	/*  should be 380000, but something needs to be at 0x3c0000?  */
	} else if (strcmp(device_name, "PMAG-FA")==0) {
		/*  px in NetBSD: TODO  */
		fb = dev_fb_init(cpu, mem, baseaddr + 0, VFB_GENERIC, 1024, 768, 1024, 768, 24, "PMAG-FA");
		/*  dev_bt459_init(mem, baseaddr + 0x200000, fb->rgb_palette, 24);  */
		rom_offset = 0x300000;
	} else if (strcmp(device_name, "PMAG-DV")==0) {
		/*  xcfb in NetBSD: TODO  */
		fb = dev_fb_init(cpu, mem, baseaddr + 0x2000000, VFB_DEC_MAXINE, 0, 0, 0, 0, 0, "PMAG-DV");
		/*  TODO:  not yet usable, needs a IMS332 vdac  */
		rom_offset = 0x3c0000;
	} else if (device_name[0] != '\0')
		fatal("warning: unknown TURBOchannel device name \"%s\"\n", device_name);

	memory_device_register(mem, "turbochannel", baseaddr + rom_offset, DEV_TURBOCHANNEL_LEN, dev_turbochannel_access, d);
}


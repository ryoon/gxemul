/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: pci_dec21030.c,v 1.1 2004-01-06 23:19:32 debug Exp $
 *
 *  DEC 21030 "tga" graphics.
 *
 *  Resolutions that seem to be possible:  640x480, 1024x768, 1280x1024.
 *  8 bits, perhaps others? (24 bit?)
 *
 *  NetBSD should say something like this:
 *
 *	tga0 at pci0 dev 12 function 0: TGA2 pass 2, board type T8-02
 *	tga0: 1280 x 1024, 8bpp, Bt485 RAMDAC
 *
 *  TODO: This device is far from complete.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"
#include "tgareg.h"

int dec21030_default_xsize = 640;
int dec21030_default_ysize = 480;


struct dec21030_data {
	uint32_t	pixel_mask;
};


/*
 *  pci_dec21030_rr():
 *
 *  See http://mail-index.netbsd.org/port-arc/2001/08/13/0000.html
 *  for more info.
 */
uint32_t pci_dec21030_rr(int reg)
{
	switch (reg) {
	case 0x00:
		return PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21030 << 16);
	case 0x04:
		return 0x02800087;
	case 0x08:
		return 0x03800003;
		/*  return PCI_CLASS_CODE(PCI_CLASS_DISPLAY, PCI_SUBCLASS_DISPLAY_VGA, 0) + 0x03;  */
	case 0x0c:
		return 0x0000ff00;
	case 0x10:
		return 0x00000000 + 8;		/*  address  (8=prefetchable)  */
	case 0x30:
		return 0x08000001;
	case 0x3c:
		return 0x00000100;	/*  interrupt pin A (?)  */
	default:
		return 0;
	}
}


/*
 *  dev_dec21030_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_dec21030_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	struct dec21030_data *d = extra;
	uint64_t idata, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	/*  Read from/write to the dec21030:  */
        switch (relative_addr) {

	/*  Board revision  */
	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_GREV:
		odata = 0x04;		/*  01,02,03,04 (rev0) and 20,21,22 (rev1) are allowed  */
		break;

	/*  Pixel mask:  */
	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_GPXR_S:		/*  "one-shot"  */
	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_GPXR_P:		/*  persistant  */
		if (writeflag == MEM_WRITE)
			d->pixel_mask = idata;
		else
			odata = d->pixel_mask;
		break;

	/*  Horizonsal size:  */
	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_VHCR:
		odata = dec21030_default_xsize / 4;	/*  lowest 9 bits  */
		break;

	/*  Vertical size:  */
	case TGA_MEM_CREGS + sizeof(uint32_t) * TGA_REG_VVCR:
		odata = dec21030_default_ysize;		/*  lowest 11 bits  */
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ dec21030: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		else
			debug("[ dec21030: unimplemented read from address 0x%x ]\n", relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  pci_dec21030_init():
 */
void pci_dec21030_init(struct cpu *cpu, struct memory *mem)
{
	struct dec21030_data *d;

	d = malloc(sizeof(struct dec21030_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec21030_data));

	/*  TODO:  this address is based on what NetBSD/arc uses...  fix this  */
	memory_device_register(mem, "dec21030", 0x100000000000, 0x200000, dev_dec21030_access, d);

	/*  TODO:  I have no idea about how/where this framebuffer should be in relation to the pci device  */
	dev_fb_init(cpu, mem, 0x100000201000, VFB_GENERIC,
	    dec21030_default_xsize, dec21030_default_ysize,
	    dec21030_default_xsize, dec21030_default_ysize, 8, "TGA");
}


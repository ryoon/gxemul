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
 *  $Id: pci_ahc.c,v 1.4 2004-07-03 16:25:12 debug Exp $
 *
 *  Adaptec AHC SCSI controller.
 *
 *  NetBSD should say something like this, on SGI-IP32:
 *	ahc0 at pci0 dev 1 function 0
 *	ahc0: interrupting at crime irq 0
 *	ahc0: aic7880 Wide Channel A, SCSI Id=7, 16/255 SCBs
 *	ahc0: Host Adapter Bios disabled.  Using default SCSI device parameters
 *
 *  TODO:  This more or less just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"


struct ahc_data {
	int		dummy;
};


/*
 *  pci_ahc_rr():
 */
uint32_t pci_ahc_rr(int reg)
{
	/*  Numbers taken from a Adaptec 2940U:  */
	/*  http://mail-index.netbsd.org/netbsd-bugs/2000/04/29/0000.html  */

	switch (reg) {
	case 0x00:
		return PCI_VENDOR_ADP + ((uint32_t)PCI_PRODUCT_ADP_2940U << 16);
	case 0x04:
		return 0x02900007;
	case 0x08:
		return PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_MASS_STORAGE_SCSI, 0) + 0x1;		/*  Revision ?  */
	case 0x0c:
		return 0x00004008;
	case 0x10:
		return 1;	/*  1 = type i/o. 0x0000e801;  address?  */
	case 0x14:
		return 0;	/*  0xf1002000;  */
	case 0x18:
		return 0x00000000;
	case 0x1c:
		return 0x00000000;
	case 0x20:
		return 0x00000000;
	case 0x24:
		return 0x00000000;
	case 0x28:
		return 0x00000000;
	case 0x2c:
		return 0; /* 0x78819004; subsystem vendor id  ???  */
	case 0x30:
		return 0xef000000;
	case 0x34:
		return 0x000000dc;
	case 0x38:
		return 0x00000000;
	case 0x3c:
		return 0x0; /* 0x08080109 interrupt pin ?  */
	default:
		return 0;
	}
}


/*
 *  dev_ahc_access():
 */
int dev_ahc_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
/*	struct ahc_data *d = extra;  */
	uint64_t idata, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	default:
		if (writeflag == MEM_WRITE)
			debug("[ ahc: unimplemented write to address 0x%x, data=0x%02x ]\n", relative_addr, idata);
		else
			debug("[ ahc: unimplemented read from address 0x%x ]\n", relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  pci_ahc_init():
 */
void pci_ahc_init(struct cpu *cpu, struct memory *mem)
{
	struct ahc_data *d;

	d = malloc(sizeof(struct ahc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ahc_data));

	/*  TODO:  this address is based on what NetBSD/sgimips uses...  fix this  */
	memory_device_register(mem, "ahc", 0x18000000, 0x100,
	    dev_ahc_access, d);
}


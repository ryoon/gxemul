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
 *  $Id: pci_ahc.c,v 1.20 2005-10-20 22:49:07 debug Exp $
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

#include "bus_pci.h"
#include "cpu.h"
#include "devices.h"
#include "memory.h"
#include "misc.h"

#include "aic7xxx_reg.h"


/* #define	AHC_DEBUG
   #define debug fatal */


#define PCI_VENDOR_ADP  0x9004          /* Adaptec */
#define PCI_VENDOR_ADP2 0x9005          /* Adaptec (2nd PCI Vendor ID) */
#define PCI_PRODUCT_ADP_2940U   0x8178          /* AHA-2940 Ultra */
#define PCI_PRODUCT_ADP_2940UP  0x8778          /* AHA-2940 Ultra Pro */

#define	DEV_AHC_LENGTH		0x100

struct ahc_data {
	unsigned char	reg[DEV_AHC_LENGTH];
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
		return PCI_VENDOR_ADP +
		    ((uint32_t)PCI_PRODUCT_ADP_2940U << 16);
	case 0x04:
		return 0x02900007;
	case 0x08:
		/*  Revision ?  */
		return PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
		    PCI_SUBCLASS_MASS_STORAGE_SCSI, 0) + 0x1;
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
		return 0x08080109;	/*  interrupt pin A?  */
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
	struct ahc_data *d = extra;
	uint64_t idata, odata = 0;
	int ok = 0;
	char *name = NULL;

	idata = memory_readmax64(cpu, data, len);

	/*  YUCK! SGI uses reversed order inside 32-bit words:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		relative_addr = (relative_addr & ~0x3)
		    | (3 - (relative_addr & 3));

	relative_addr %= DEV_AHC_LENGTH;

	if (len != 1)
		fatal("[ ahc: ERROR! Unimplemented len %i ]\n", len);

	if (writeflag == MEM_READ)
		odata = d->reg[relative_addr];

	switch (relative_addr) {

	case SCSIID:
		if (writeflag == MEM_READ) {
			ok = 1; name = "SCSIID";
			odata = 0;
		} else {
			fatal("[ ahc: write to SCSIOFFSET, data = 0x"
			    "%02x: TODO ]\n", (int)idata);
		}
		break;

	case KERNEL_QINPOS:
		if (writeflag == MEM_WRITE) {

			/*  TODO  */

			d->reg[INTSTAT] |= SEQINT;
		}
		break;

	case SEECTL:
		ok = 1; name = "SEECTL";
		if (writeflag == MEM_WRITE)
			d->reg[relative_addr] = idata;
		odata |= SEERDY;
		break;

	case SCSICONF:
		ok = 1; name = "SCSICONF";
		if (writeflag == MEM_READ) {
			odata = 0;
		} else {
			fatal("[ ahc: write to SCSICONF, data = 0x%02x:"
			    " TODO ]\n", (int)idata);
		}
		break;

	case SEQRAM:
	case SEQADDR0:
	case SEQADDR1:
		/*  TODO: This is just a dummy.  */
		break;

	case HCNTRL:
		ok = 1; name = "HCNTRL";
		if (writeflag == MEM_WRITE)
			d->reg[relative_addr] = idata;
		break;

	case INTSTAT:
		ok = 1; name = "INTSTAT";
		if (writeflag == MEM_WRITE)
			fatal("[ ahc: write to INTSTAT? data = 0x%02x ]\n",
			    (int)idata);
		break;

	case CLRINT:
		if (writeflag == MEM_READ) {
			ok = 1; name = "ERROR";
			/*  TODO  */
		} else {
			ok = 1; name = "CLRINT";
			if (idata & ~0xf)
				fatal("[ ahc: write to CLRINT: 0x%02x "
				    "(TODO) ]\n", (int)idata);
			/*  Clear the lowest 4 bits of intstat:  */
			d->reg[INTSTAT] &= ~(idata & 0xf);
		}
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ ahc: UNIMPLEMENTED write to address 0x%x, "
			    "data=0x%02x ]\n", (int)relative_addr, (int)idata);
		else
			fatal("[ ahc: UNIMPLEMENTED read from address 0x%x ]\n",
			    (int)relative_addr);
	}

#if 0
cpu_interrupt(cpu, 0x200);
#endif

#ifdef AHC_DEBUG
	if (ok) {
		if (name == NULL) {
			if (writeflag == MEM_WRITE)
				debug("[ ahc: write to address 0x%x: 0x"
				    "%02x ]\n", (int)relative_addr, (int)idata);
			else
				debug("[ ahc: read from address 0x%x: 0x"
				    "%02x ]\n", (int)relative_addr, (int)odata);
		} else {
			if (writeflag == MEM_WRITE)
				debug("[ ahc: write to %s: 0x%02x ]\n",
				    name, (int)idata);
			else
				debug("[ ahc: read from %s: 0x%02x ]\n",
				    name, (int)odata);
		}
	}
#endif

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  pci_ahc_init():
 */
void pci_ahc_init(struct machine *machine, struct memory *mem)
{
	struct ahc_data *d;

	d = malloc(sizeof(struct ahc_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ahc_data));

	/*  TODO:  this address is based on what NetBSD/sgimips uses
	    on SGI IP32 (O2). Fix this.  */

	memory_device_register(mem, "ahc", 0x18000000, DEV_AHC_LENGTH,
	    dev_ahc_access, d, MEM_DEFAULT, NULL);

	/*  OpenBSD/sgi snapshots sometime between 2005-03-11 and
	    2005-04-04 changed to using 0x1a000000:  */
	dev_ram_init(machine, 0x1a000000, 0x2000000, DEV_RAM_MIRROR,
	    0x18000000);
}


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
 */

/*
 *  dev_gt.c  --  the "gt" device used in Cobalt machines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"


struct gt_data {
	int	reg[8];
	int	irqnr;

	int	pci_addr;
};


#define	PCI_VENDOR_GALILEO			0x11ab
#define	PCI_PRODUCT_GALILEO_GT64011		0x4146

#define	PCI_VENDOR_DEC				0x1011
#define	PCI_PRODUCT_DEC_21142			0x0019

#define	PCI_VENDOR_SYMBIOS			0x1000
#define	PCI_PRODUCT_SYMBIOS_860			0x0006

#define	PCI_VENDOR_VIATECH			0x1106
#define	PCI_PRODUCT_VIATECH_VT82C586_ISA	0x0586


/*
 *  dev_gt_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_gt_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	uint64_t idata = 0, odata=0, odata_set=0;
	struct gt_data *d = extra;

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

	switch (relative_addr) {
	case 0xcf8:	/*  PCI ADDR  */
		if (writeflag == MEM_WRITE) {
			debug("[ gt write to  PCI ADDR: data = 0x%08lx ]\n", (long)idata);
			d->pci_addr = idata;
			return 1;
		} else {
			debug("[ gt read from PCI ADDR (data = 0x%08lx) ]\n", (long)d->pci_addr);
			odata = d->pci_addr;
			odata_set = 1;
		}
		break;
	case 0xcfc:	/*  PCI DATA  */
		if (writeflag == MEM_WRITE) {
			debug("[ gt write to PCI DATA: data = 0x%08lx ]\n", (long)idata);
			return 0;
		} else {
			switch (d->pci_addr) {
			case 0x80000000 + (0 << 16) + (0 << 11):	/*  bus 0, device 0  */
				odata = PCI_VENDOR_GALILEO + (PCI_PRODUCT_GALILEO_GT64011 << 16);
				break;
			case 0x80000000 + (0 << 16) + (7 << 11):	/*  bus 0, device 7  */
				odata = PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21142 << 16);
				break;
/*			case 0x80000000 + (0 << 16) + (8 << 11):  */	/*  bus 0, device 8  */
/*				odata = PCI_VENDOR_SYMBIOS + (PCI_PRODUCT_SYMBIOS_860 << 16);  */
/*				break;  */
/*			case 0x80000000 + (0 << 16) + (9 << 11):  */	/*  bus 0, device 9  */
/*				odata = PCI_VENDOR_VIATECH + (PCI_PRODUCT_VIATECH_VT82C586_ISA << 16);  */
/*				break;  */
/*			case 0x80000000 + (0 << 16) + (12 << 11):  */	/*  bus 0, device 12  */
/*				odata = PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21142 << 16);  */
/*				break;  */
			default:
				if ((d->pci_addr & 0xff) == 0)
					odata = 0xffffffff;
				else {
					switch (d->pci_addr) {
					case 0x80000008:	/*  GT-64011 revision: 1  */
						odata = 0x1;
						break;
					case 0x80003804:	/*  tulip  */
						odata = 0xffffffff;
						break;
					case 0x80003808:	/*  tulip card revision: 4.1 */
						odata = 0x41;
						break;
					case 0x80003810:	/*  tulip  */
						odata = 0x9ca00001;	/*  1ca00000, I/O space  */
						break;
					case 0x80003814:	/*  tulip  */
						odata = 0x9ca10000;	/*  1ca10000, mem space  */
						break;
					case 0x8000383c:	/*  tulip card  */
						odata = 0x00000100;	/*  interrupt pin A  */
						break;
					default:
						odata = 0;
					}
					debug("[ gt read from PCI DATA, addr = 0x%x, returning: 0x%x ]\n",
					    (int)d->pci_addr, odata);
				}
			}
			odata_set = 1;
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ gt read from addr 0x%x ]\n", (int)relative_addr);
			odata = d->reg[relative_addr];
			odata_set = 1;
		} else {
			debug("[ gt write to addr 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" ]\n");
			d->reg[relative_addr] = idata;
			return 1;
		}
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
		return 1;
	}

	return 0;
}


/*
 *  dev_gt_init():
 */
void dev_gt_init(struct memory *mem, uint64_t baseaddr, int irq_nr)
{
	struct gt_data *d;

	d = malloc(sizeof(struct gt_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct gt_data));
	d->irqnr = irq_nr;

	memory_device_register(mem, "gt", baseaddr, DEV_GT_LENGTH, dev_gt_access, d);
}


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
 *  $Id: bus_pci.c,v 1.1 2004-01-06 06:47:00 debug Exp $
 *  
 *  This is a generic PCI bus device, used by even lower level devices.
 *  For example, the "gt" device used in Cobalt machines contains a PCI
 *  device.
 *
 *  TODO:  This more or less just a dummy bus device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"

#include "bus_pci.h"


/*
 *  bus_pci_access():
 *
 *  relative_addr should be either BUS_PCI_ADDR or BUS_PCI_DATA.  The uint64_t pointed
 *  to by data should contain the word to be written to the pci bus, or a placeholder
 *  for information read from the bus.
 *
 *  Returns 1 if ok, 0 on error.
 */
int bus_pci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, uint64_t *data, int writeflag, struct pci_data *pci_data)
{
	if (writeflag == MEM_READ)
		*data = 0;

	switch (relative_addr) {
	case BUS_PCI_ADDR:
		if (writeflag == MEM_WRITE) {
			debug("[ bus_pci: write to  PCI ADDR: data = 0x%016llx ]\n", (long long)*data);
			pci_data->pci_addr = *data;
		} else {
			debug("[ bus_pci: read from PCI ADDR (data = 0x%016llx) ]\n", (long long)pci_data->pci_addr);
			*data = pci_data->pci_addr;
		}
		break;
	case BUS_PCI_DATA:
		if (writeflag == MEM_WRITE) {
			debug("[ bus_pci: write to PCI DATA: data = 0x%016llx ]\n", (long long)*data);
		} else {
			switch (pci_data->pci_addr) {
			case 0x80000000 + (0 << 16) + (0 << 11):	/*  bus 0, device 0  */
				*data = PCI_VENDOR_GALILEO + (PCI_PRODUCT_GALILEO_GT64011 << 16);
				break;
			case 0x80000000 + (0 << 16) + (7 << 11):	/*  bus 0, device 7  */
				*data = PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21142 << 16);
				break;
/*			case 0x80000000 + (0 << 16) + (8 << 11):  */	/*  bus 0, device 8  */
/*				*data = PCI_VENDOR_SYMBIOS + (PCI_PRODUCT_SYMBIOS_860 << 16);  */
/*				break;  */
/*			case 0x80000000 + (0 << 16) + (9 << 11):  */	/*  bus 0, device 9  */
/*				*data = PCI_VENDOR_VIATECH + (PCI_PRODUCT_VIATECH_VT82C586_ISA << 16);  */
/*				break;  */
/*			case 0x80000000 + (0 << 16) + (12 << 11):  */	/*  bus 0, device 12  */
/*				*data = PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21142 << 16);  */
/*				break;  */
			default:
				if ((pci_data->pci_addr & 0xff) == 0)
					*data = 0xffffffff;
				else {
					switch (pci_data->pci_addr) {
					case 0x80000008:	/*  GT-64011 revision: 1  */
						*data = 0x1;
						break;
					case 0x80003804:	/*  tulip  */
						*data = 0xffffffff;
						break;
					case 0x80003808:	/*  tulip card revision: 4.1 */
						*data = 0x41;
						break;
					case 0x80003810:	/*  tulip  */
						*data = 0x9ca00001;	/*  1ca00000, I/O space  */
						break;
					case 0x80003814:	/*  tulip  */
						*data = 0x9ca10000;	/*  1ca10000, mem space  */
						break;
					case 0x8000383c:	/*  tulip card  */
						*data = 0x00000100;	/*  interrupt pin A  */
						break;
					default:
						*data = 0;
					}
					debug("[ bus_pci: read from PCI DATA, addr = 0x%08lx, returning: 0x%08lx ]\n",
					    (long)pci_data->pci_addr, (long)*data);
				}
			}
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ bus_pci: read from unimplemented addr 0x%x ]\n", (int)relative_addr);
			*data = 0;
		} else {
			debug("[ bus_pci: write to unimplemented addr 0x%x:", (int)relative_addr);
		}
	}

	return 1;
}


/*
 *  bus_pci_init():
 *
 *  This doesn't register a device, but instead returns a pointer to a struct
 *  which should be passed to bus_pci_access() when accessing the PCI bus.
 *
 *  TODO:  Should 'mem' even be an incoming parameter here?
 */
struct pci_data *bus_pci_init(struct memory *mem)
{
	struct pci_data *d;

	d = malloc(sizeof(struct pci_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct pci_data));
	return d;
}


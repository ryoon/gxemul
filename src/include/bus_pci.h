#ifndef	PCI_BUS_H
#define	PCI_BUS_H

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
 */

#include "memory.h"
#include "misc.h"

struct pci_data {
	uint32_t	pci_addr;
};

#define	BUS_PCI_ADDR	0xcf8
#define	BUS_PCI_DATA	0xcfc

#define	PCI_VENDOR_GALILEO			0x11ab
#define	PCI_PRODUCT_GALILEO_GT64011		0x4146

#define	PCI_VENDOR_DEC				0x1011
#define	PCI_PRODUCT_DEC_21142			0x0019

#define	PCI_VENDOR_SYMBIOS			0x1000
#define	PCI_PRODUCT_SYMBIOS_860			0x0006

#define	PCI_VENDOR_VIATECH			0x1106
#define	PCI_PRODUCT_VIATECH_VT82C586_ISA	0x0586


int bus_pci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, uint64_t *data, int writeflag, struct pci_data *pci_data);
struct pci_data *bus_pci_init(struct memory *mem);

#endif	/*  PCI_BUS_H  */

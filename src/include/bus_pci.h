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

struct pci_device {
	int		bus, device, function;

	void		(*init)(struct memory *mem);
	uint32_t	(*read_register)(int reg);

	struct pci_device *next;
};

struct pci_data {
	uint32_t	pci_addr;
	struct pci_device *first_device;
};

#define	BUS_PCI_ADDR	0xcf8
#define	BUS_PCI_DATA	0xcfc


#include "pcidevs.h"
#include "pcireg.h"


/*  bus_pci.c:  */
int bus_pci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, uint64_t *data, int writeflag, struct pci_data *pci_data);
void bus_pci_add(struct pci_data *pci_data, struct memory *mem,
	int bus, int device, int function,
	void (*init)(struct memory *mem), uint32_t (*read_register)(int reg));
struct pci_data *bus_pci_init(struct memory *mem);


/*
 *  Individual devices:
 */

/*  dec21143:  */
uint32_t pci_dec21143_rr(int reg);
void pci_dec21143_init(struct memory *mem);

/*  vt82c586:  */
uint32_t pci_vt82c586_isa_rr(int reg);
void pci_vt82c586_isa_init(struct memory *mem);
uint32_t pci_vt82c586_ide_rr(int reg);
void pci_vt82c586_ide_init(struct memory *mem);


#endif	/*  PCI_BUS_H  */

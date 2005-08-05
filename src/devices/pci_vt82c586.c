/*
 *  Copyright (C) 2004  Anders Gavare.  All rights reserved.
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
 *  $Id: pci_vt82c586.c,v 1.15 2005-08-05 07:09:30 debug Exp $
 *
 *  VIATECH VT82C586 devices:
 *
 *	vt82c586_isa	PCI->ISA bridge
 *	vt82c586_ide	IDE controller
 *
 *  TODO:  This more or less just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "devices.h"
#include "bus_pci.h"


#define PCI_VENDOR_VIATECH                0x1106  /* VIA Technologies */
#define PCI_PRODUCT_VIATECH_VT82C586_IDE  0x1571  /* VT82C586 (Apollo VP)
					             IDE Controller */
#define PCI_PRODUCT_VIATECH_VT82C586_ISA  0x0586  /* VT82C586 (Apollo VP)
						     PCI-ISA Bridge */


/*
 *  pci_vt82c586_isa_rr():
 */
uint32_t pci_vt82c586_isa_rr(int reg)
{
	/*  NetBSD reads from 0x04, 0x08, 0x3c, 0x0c, 0x2c during init  */

	switch (reg) {
	case 0x00:
		return PCI_VENDOR_VIATECH +
		    (PCI_PRODUCT_VIATECH_VT82C586_ISA << 16);
	case 0x04:
		return 0xffffffff;	/*  ???  */
	case 0x08:
		/*  Revision 37 or 39  */
		return PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
		    PCI_SUBCLASS_BRIDGE_ISA, 0) + 39;
	case 0x0c:
		/*  Bit 7 of Header-type byte ==> multi-function device  */
		return 0x00800000;
	default:
		return 0;
	}
}


/*
 *  pci_vt82c586_isa_init():
 */
void pci_vt82c586_isa_init(struct machine *machine, struct memory *mem)
{
}


/*
 *  pci_vt82c586_ide_rr():
 */
uint32_t pci_vt82c586_ide_rr(int reg)
{
	/*  NetBSD reads from 0x04, 0x08, 0x3c, 0x0c, 0x2c during init  */

	switch (reg) {
	case 0x00:
		return PCI_VENDOR_VIATECH +
		    (PCI_PRODUCT_VIATECH_VT82C586_IDE << 16);
	case 0x04:
		return 0xffffffff;	/*  ???  */
	case 0x08:
		/*  Possibly not correct:  */
		return PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
		    PCI_SUBCLASS_MASS_STORAGE_IDE, 0) + 0x01;
	case 0x40:	/*  APO_IDECONF  */
		/*  channel 0 and 1 enabled  */
		return 0x00000003;
	default:
		return 0;
	}
}


/*
 *  pci_vt82c586_ide_init():
 */
void pci_vt82c586_ide_init(struct machine *machine, struct memory *mem)
{
	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_COBALT:
		dev_wdc_init(machine, mem, 0x100001f0, 8 + 14, 0);	/*  primary  */
		dev_wdc_init(machine, mem, 0x10000170, 8 + 15, 2);	/*  secondary */
		break;

	case MACHINE_EVBMIPS:
		/*  TODO: Irqs...  */
		dev_wdc_init(machine, mem, 0x180001f0, 8+14, 0);/*  primary  */
		dev_wdc_init(machine, mem, 0x18000170, 8+15, 2);/*  secondary */
		break;

	default:fatal("pci_vt82c586_ide_init(): unimplemented machine type\n");
		exit(1);
	}
}


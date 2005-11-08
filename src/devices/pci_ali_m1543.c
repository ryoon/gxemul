/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: pci_ali_m1543.c,v 1.4 2005-11-08 11:01:46 debug Exp $
 *
 *  Acer Labs M5229 PCI-IDE (UDMA) controller.
 *  Acer Labs M1543 PCI->ISA bridge.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define PCI_VENDOR_ALI			0x10b9
#define PCI_PRODUCT_ALI_M1543		0x1533	/*  NOTE: not 1543  */
#define	PCI_PRODUCT_ALI_M5229		0x5229


PCIINIT(ali_m1543)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M1543));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
	    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0xc3);

	PCI_SET_DATA(PCI_BHLC_REG,
	    PCI_BHLC_CODE(0,0, 1 /* multi-function */, 0,0));
}


PCIINIT(ali_m5229)
{
	PCI_SET_DATA(PCI_ID_REG,
	    PCI_ID_CODE(PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M5229));

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_IDE, 0x60) + 0xc1);

#if 0
/*  TODO:  */
case 0x10:
	return 0x000001f1;
case 0x14:
	return 0x000003f7;
case 0x18:
	return 0x00000171;
case 0x1c:
	return 0x00000377;
case 60:
	return 0x8e;	/*  ISA int 14  */
case 61:
	return 0x04;
#endif


	/*
	 *  TODO: The check for machine type shouldn't be here?
	 */

	switch (machine->machine_type) {

	case MACHINE_CATS:
		device_add(machine, "wdc addr=0x7c0001f0 irq=46");/* primary  */
		/*  The secondary channel is disabled. TODO: fix this.  */
		/*  device_add(machine, "wdc addr=0x7c000170 irq=47");  */
		break;

	default:fatal("ali_m5229: unimplemented machine type\n");
		exit(1);
	}
}


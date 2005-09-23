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
 *  $Id: pci_symphony_83c553.c,v 1.1 2005-09-23 11:47:01 debug Exp $
 *
 *  Symphony Labs 83C553 PCI->ISA bridge.
 *
 *  TODO:  This more or less just a dummy device, so far.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


#define PCI_VENDOR_SYMPHONY		0x10ad
#define PCI_PRODUCT_SYMPHONY_83C553	0x0565


/*
 *  pci_symphony_83c553_rr():
 */
uint32_t pci_symphony_83c553_rr(int reg)
{
	switch (reg) {
	case 0x00:
		return PCI_VENDOR_SYMPHONY +
		    (PCI_PRODUCT_SYMPHONY_83C553 << 16);
	case 0x04:
		return 0xffffffff;	/*  ???  */
	case 0x08:
		/*  Revision  */
		return PCI_CLASS_CODE(PCI_CLASS_BRIDGE,
		    PCI_SUBCLASS_BRIDGE_ISA, 0) + 0x10;
	case 0x0c:
		/*  Bit 7 of Header-type byte ==> multi-function device  */
		return 0x00800000;
	default:
		return 0;
	}
}


/*
 *  pci_symphony_83c553_init():
 */
void pci_symphony_83c553_init(struct machine *machine, struct memory *mem)
{
}


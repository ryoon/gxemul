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
 *  $Id: pci_dec21143.c,v 1.5 2005-01-09 01:55:26 debug Exp $
 *
 *  DEC 21143 PCI ethernet.
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


/*
 *  pci_dec21143_rr():
 */
uint32_t pci_dec21143_rr(int reg)
{
	switch (reg) {
	case 0x00:
		return PCI_VENDOR_DEC + (PCI_PRODUCT_DEC_21142 << 16);
	case 0x04:
		return 0xffffffff;
	case 0x08:
		return PCI_CLASS_CODE(PCI_CLASS_NETWORK, PCI_SUBCLASS_NETWORK_ETHERNET, 0) + 0x41;		/*  Revision 4.1  */
	case 0x10:
		return 0x9ca00001;	/*  1ca00000, I/O space  (I have no idea about these...)  */
	case 0x14:
		return 0x9ca10000;	/*  1ca10000, mem space  (I have no idea about these...)  */
	case 0x3c:
		return 0x00000100;	/*  interrupt pin A  */
	default:
		return 0;
	}
}


/*
 *  pci_dec21143_init():
 */
void pci_dec21143_init(struct cpu *cpu, struct memory *mem)
{
}


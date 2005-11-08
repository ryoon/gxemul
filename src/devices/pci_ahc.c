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
 *  $Id: pci_ahc.c,v 1.21 2005-11-08 11:01:46 debug Exp $
 *
 *  PCI glue for the Adaptec AHC SCSI controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_pci.h"
#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "memory.h"
#include "misc.h"


#define PCI_VENDOR_ADP  0x9004          /* Adaptec */
#define PCI_VENDOR_ADP2 0x9005          /* Adaptec (2nd PCI Vendor ID) */
#define PCI_PRODUCT_ADP_2940U   0x8178          /* AHA-2940 Ultra */
#define PCI_PRODUCT_ADP_2940UP  0x8778          /* AHA-2940 Ultra Pro */


PCIINIT(ahc)
{
	/*  Numbers taken from a Adaptec 2940U:  */
	/*  http://mail-index.netbsd.org/netbsd-bugs/2000/04/29/0000.html  */

	PCI_SET_DATA(PCI_ID_REG, PCI_ID_CODE(PCI_VENDOR_ADP,
	    PCI_PRODUCT_ADP_2940U));

	PCI_SET_DATA(PCI_COMMAND_STATUS_REG, 0x02900007);

	PCI_SET_DATA(PCI_CLASS_REG,
	    PCI_CLASS_CODE(PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_SCSI, 0) + 0x01);

	PCI_SET_DATA(PCI_BHLC_REG, 0x00004008);

	/*  1 = type i/o. 0x0000e801;  address?  */
	/*  second address reg = 0xf1002000?  */
	PCI_SET_DATA(PCI_MAPREG_START + 0x00, 0x00000001);
	PCI_SET_DATA(PCI_MAPREG_START + 0x04, 0x00000000);

	PCI_SET_DATA(PCI_MAPREG_START + 0x08, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x0c, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x10, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x14, 0x00000000);
	PCI_SET_DATA(PCI_MAPREG_START + 0x18, 0x00000000);

	/*  Subsystem vendor ID? 0x78819004?  */
	PCI_SET_DATA(PCI_MAPREG_START + 0x1c, 0x00000000);

	PCI_SET_DATA(0x30, 0xef000000);
	PCI_SET_DATA(PCI_CAPLISTPTR_REG, 0x000000dc);
	PCI_SET_DATA(0x38, 0x00000000);
	PCI_SET_DATA(PCI_INTERRUPT_REG, 0x08080109);	/*  interrupt pin A  */

	/*  TODO:  this address is based on what NetBSD/sgimips uses
	    on SGI IP32 (O2). Fix this.  */

	device_add(machine, "ahc addr=0x18000000");

	/*  OpenBSD/sgi snapshots sometime between 2005-03-11 and
	    2005-04-04 changed to using 0x1a000000:  */
	dev_ram_init(machine, 0x1a000000, 0x2000000, DEV_RAM_MIRROR,
	    0x18000000);
}


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
 *  $Id: machine_testppc.c,v 1.1 2005-12-20 21:19:17 debug Exp $
 */

#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(testppc)
{
	char tmpstr[1000];

	machine->machine_name = "PPC test machine";

	/*  TODO: interrupt for PPC?  */

#if 0
	snprintf(tmpstr, sizeof(tmpstr), "cons addr=0x%llx irq=0",
	    (long long)DEV_CONS_ADDRESS);

                cons_data = device_add(machine, tmpstr);
                machine->main_console_handle = cons_data->console_handle;
                 
                snprintf(tmpstr, sizeof(tmpstr), "mp addr=0x%llx",
                    (long long)DEV_MP_ADDRESS);
                device_add(machine, tmpstr);
                
                fb = dev_fb_init(machine, mem, DEV_FB_ADDRESS, 
VFB_GENERIC,
                    640,480, 640,480, 24, "testppc generic"); 

                snprintf(tmpstr, sizeof(tmpstr), "disk addr=0x%llx",
                    (long long)DEV_DISK_ADDRESS);
                device_add(machine, tmpstr);
                
                snprintf(tmpstr, sizeof(tmpstr), "ether addr=0x%llx 
irq=0",
                    (long long)DEV_ETHER_ADDRESS);
                device_add(machine, tmpstr);
#endif

	return 1;
}


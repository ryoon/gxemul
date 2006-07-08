/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_fbctrl.c,v 1.1 2006-07-08 10:25:48 debug Exp $
 *
 *  A "framebuffer control" device. It can be used to manipulate the
 *  framebuffer device in testmachines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "cpu_mips.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "testmachine/dev_fb.h"


struct fb_data {
	int		dummy;
};


DEVICE_ACCESS(fbctrl)
{
	struct fbctrl_data *d = extra;
	int i, which_cpu;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
	        idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {

	default:
		fatal("[ dev_fbctrl: unimplemented relative addr 0x%x ]\n",
		    (int)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(fbctrl)
{
	struct fbctrl_data *d;
	int n;

	d = malloc(sizeof(struct fbctrl_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct fbctrl_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, DEV_FBCTRL_LENGTH, dev_fbctrl_access, d,
	    DM_DEFAULT, NULL);

	return 1;
}


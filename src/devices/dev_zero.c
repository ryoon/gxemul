/*
 *  Copyright (C) 2004-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_zero.c,v 1.4 2006-01-01 13:17:18 debug Exp $
 *  
 *  A simple device which returns zero for all reads, and discards all writes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


/*
 *  dev_zero_access():
 */
DEVICE_ACCESS(zero)
{
	if (writeflag == MEM_READ) {
		unsigned int i;
		for (i=0; i<len; i++)
			data[i] = 0;
	}

	return 1;
}


/*
 *  devinit_zero():
 */
int devinit_zero(struct devinit *devinit)
{
	memory_device_register(devinit->machine->memory,
	    devinit->name, devinit->addr, devinit->len,
	    dev_zero_access, NULL, DM_DEFAULT, NULL);

	return 1;
}


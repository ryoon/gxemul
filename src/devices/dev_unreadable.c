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
 *   
 *
 *  $Id: dev_unreadable.c,v 1.1 2004-06-12 12:14:53 debug Exp $
 *  
 *  A dummy device which returns memory read errors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"


/*
 *  dev_unreadable_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_unreadable_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	return 0;
}


/*
 *  dev_unreadable_init():
 */
void dev_unreadable_init(struct memory *mem, uint64_t baseaddr, uint64_t len)
{
	memory_device_register(mem, "unreadable", baseaddr, len, dev_unreadable_access, NULL);
}


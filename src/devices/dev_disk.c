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
 *  $Id: dev_disk.c,v 1.5 2005-07-15 09:46:24 debug Exp $
 *
 *  Basic "Disk" device. This is a simple test device which can be used to
 *  read and write data from disk devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


struct disk_data {
	int64_t		offset;
	int		disk_id;
	int		command;
	int		status;
	unsigned char	buf[512];
};


/*
 *  dev_disk_buf_access():
 */
int dev_disk_buf_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct disk_data *d = (struct disk_data *) extra;

	if (writeflag == MEM_WRITE)
		memcpy(d->buf + relative_addr, data, len);
	else
		memcpy(data, d->buf + relative_addr, len);
	return 1;
}


/*
 *  dev_disk_access():
 */
int dev_disk_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	struct disk_data *d = (struct disk_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 0x00:
		if (writeflag == MEM_READ) {
			odata = d->offset;
		} else {
			d->offset = idata;
		}
		break;
	case 0x10:
		if (writeflag == MEM_READ) {
			odata = d->disk_id;
		} else {
			d->disk_id = idata;
		}
		break;
	case 0x20:
		if (writeflag == MEM_READ) {
			odata = d->command;
		} else {
			d->command = idata;
			switch (d->command) {
			case 0:	d->status = diskimage_access(cpu->machine,
				     d->disk_id, DISKIMAGE_SCSI, 0,
				     d->offset, d->buf, sizeof(d->buf));
				break;
			case 1:	d->status = diskimage_access(cpu->machine,
				     d->disk_id, DISKIMAGE_SCSI, 1,
				     d->offset, d->buf, sizeof(d->buf));
				break;
			}
		}
		break;
	case 0x30:
		if (writeflag == MEM_READ) {
			odata = d->status;
		} else {
			d->status = idata;
		}
		break;
	default:if (writeflag == MEM_WRITE) {
			fatal("[ disk: unimplemented write to "
			    "offset 0x%x: data=0x%x ]\n", (int)
			    relative_addr, (int)idata);
		} else {
			fatal("[ disk: unimplemented read from "
			    "offset 0x%x ]\n", (int)relative_addr);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  devinit_disk():
 */
int devinit_disk(struct devinit *devinit)
{
	struct disk_data *d = malloc(sizeof(struct disk_data));
	size_t nlen;
	char *n1, *n2;
                 
	nlen = strlen(devinit->name) + 30;
	n1 = malloc(nlen);
	n2 = malloc(nlen);

	if (d == NULL || n1 == NULL || n2 == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct disk_data));

	snprintf(n1, nlen, "%s [control]", devinit->name);
	snprintf(n2, nlen, "%s [data buffer]", devinit->name);

	memory_device_register(devinit->machine->memory, n1,
	    devinit->addr, 4096, dev_disk_access, (void *)d,
	    MEM_DEFAULT, NULL);

	memory_device_register(devinit->machine->memory, n2,
	    devinit->addr + 4096, DEV_DISK_LENGTH - 4096, dev_disk_buf_access,
	    (void *)d, MEM_DYNTRANS_OK | MEM_DYNTRANS_WRITE_OK |
            MEM_READING_HAS_NO_SIDE_EFFECTS, NULL);

	return 1;
}


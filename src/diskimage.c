/*
 *  Copyright (C) 2003,2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: diskimage.c,v 1.11 2004-04-06 10:46:03 debug Exp $
 *
 *  Disk image support.
 *
 *  TODO:  diskimage_remove() ?
 *         Actually test diskimage_access() to see that it works.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "misc.h"
#include "diskimage.h"


struct diskimage {
	char		*fname;
	off_t		total_size;

	int		writable;
	int		is_a_cdrom;

	FILE		*f;
};


#define	MAX_DISKIMAGES		8

static struct diskimage *diskimages[MAX_DISKIMAGES];
static int n_diskimages = 0;

int logical_block_size = 512;


/**************************************************************************/


/*
 *  scsi_transfer_alloc():
 *
 *  Allocates memory for a new scsi_transfer struct, and fills it with
 *  sane data (NULL pointers).
 *  The return value is a pointer to the new struct.  If allocation
 *  failed, the program exits.
 */
struct scsi_transfer *scsi_transfer_alloc(void)
{
	struct scsi_transfer *p;

	p = malloc(sizeof(struct scsi_transfer));
	if (p == NULL) {
		fprintf(stderr, "scsi_transfer_alloc(): out of memory\n");
		exit(1);
	}

	memset(p, 0, sizeof(struct scsi_transfer));

	return p;
}


/*
 *  scsi_transfer_free():
 *
 *  Frees the space used by a scsi_transfer struct.  All buffers refered
 *  to by the scsi_transfer struct are freed.
 */
void scsi_transfer_free(struct scsi_transfer *p)
{
	if (p == NULL) {
		fprintf(stderr, "scsi_transfer_free(): p == NULL\n");
		exit(1);
	}

	if (p->msg_out != NULL)
		free(p->msg_out);
	if (p->cmd != NULL)
		free(p->cmd);
	if (p->data_out != NULL)
		free(p->data_out);

	if (p->data_in != NULL)
		free(p->data_in);
	if (p->msg_in != NULL)
		free(p->msg_in);
	if (p->status != NULL)
		free(p->status);

	free(p);
}


/*
 *  scsi_transfer_allocbuf():
 *
 *  Helper function, used by diskimage_scsicommand(), and SCSI controller
 *  devices.  Example of usage:
 *
 *	scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
 */
void scsi_transfer_allocbuf(size_t *lenp, unsigned char **pp, size_t want_len)
{
	unsigned char *p = (*pp);

	if (p != NULL) {
		printf("WARNING! scsi_transfer_allocbuf(): old pointer "
		    "was not NULL, freeing it now\n");
		free(p);
	}

	(*lenp) = want_len;
	if ((p = malloc(want_len)) == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(p, 0, want_len);
	(*pp) = p;
}


/**************************************************************************/


/*
 *  diskimage_exist():
 *
 *  Returns 1 if the specified disk_id exists, 0 otherwise.
 */
int diskimage_exist(int disk_id)
{
	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL)
		return 0;

	return 1;
}


/*
 *  diskimage_getsize():
 *
 *  Returns -1 if the specified disk_id does not exists, otherwise
 *  the size of the disk image is returned.
 */
int64_t diskimage_getsize(int disk_id)
{
	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL)
		return -1;

	return diskimages[disk_id]->total_size;
}


/*
 *  diskimage_scsicommand():
 *
 *  Perform a SCSI command on a disk image.
 *
 *  The xferp points to a scsi_transfer struct, containing msg_out, command,
 *  and data_out coming from the SCSI controller device.  This function
 *  interprets the command, and (if neccessary) creates responses in
 *  data_in, msg_in, and status.
 *
 *  Returns 1 if ok, 0 on error.
 */
int diskimage_scsicommand(int disk_id, struct scsi_transfer *xferp)
{
	int retlen;
	int64_t size, ofs;

	if (disk_id < 0 || disk_id >= n_diskimages || diskimages[disk_id]==NULL)
		return 0;

	if (xferp->cmd == NULL) {
		fatal("diskimage_scsicommand(): cmd == NULL\n");
		return 0;
	}

	if (xferp->cmd_len < 1) {
		fatal("diskimage_scsicommand(): cmd_len == %i\n",
		    xferp->cmd_len);
		return 0;
	}

	debug("[ diskimage_scsicommand(id=%i) cmd=0x%02x: ",
	    disk_id, xferp->cmd[0]);

	switch (xferp->cmd[0]) {

	case SCSICMD_TEST_UNIT_READY:
		debug("TEST_UNIT_READY");
		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: bits 765 of buf[1] contains the LUN  */

		/*  Return msg and status:  */
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;

		break;

	case SCSICMD_INQUIRY:
		debug("INQUIRY");
		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);
		if (xferp->cmd[1] != 0x00) {
			debug("WARNING: INQUIRY with cmd[1]=0x%02x not yet implemented\n");

			break;
		}

		/*  Return values:  */
		retlen = xferp->cmd[4];
		if (retlen < 36) {
			fatal("WARNING: SCSI inquiry len=%i, <36!\n", retlen);
			retlen = 36;
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, retlen);
		xferp->data_in[0] = 0x00;	/*  0x00 = Direct-access disk  */
		xferp->data_in[1] = 0x00;	/*  0x00 = non-removable  */
		xferp->data_in[2] = 0x02;	/*  SCSI-2  */
		xferp->data_in[4] = retlen - 4;	/*  Additional length  */
		xferp->data_in[6] = 0x04;	/*  ACKREQQ  */
		xferp->data_in[7] = 0x60;	/*  WBus32, WBus16  */

		/*  These must be padded with spaces:  */
		memcpy(xferp->data_in+8,  "FAKE    ", 8);
		memcpy(xferp->data_in+16, "DISK            ", 16);
		memcpy(xferp->data_in+32, "V0.0", 4);

		/*  Some data is different for CD-ROM drives:  */
		if (diskimages[disk_id]->is_a_cdrom) {
			xferp->data_in[0] = 0x05;	/*  0x05 = CD-ROM  */
			xferp->data_in[1] = 0x80;	/*  0x80 = removable  */
			memcpy(xferp->data_in+16, "CDROM           ", 16);
		}


		/*  Return msg and status:  */
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;

		break;

	case SCSIBLOCKCMD_READ_CAPACITY:
		debug("READ_CAPACITY");

		if (xferp->cmd_len != 10)
			debug(" (weird len=%i)", xferp->cmd_len);
		if (xferp->cmd[8] & 1) {
			/*  Partial Medium Indicator bit...  TODO  */
			fatal("WARNING: READ_CAPACITY with PMI bit set not yet implemented\n");
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, 8);

		size = diskimages[disk_id]->total_size / logical_block_size;
		if (diskimages[disk_id]->total_size & (logical_block_size-1))
			size ++;

		xferp->data_in[0] = (size >> 24) & 255;
		xferp->data_in[1] = (size >> 16) & 255;
		xferp->data_in[2] = (size >> 8) & 255;
		xferp->data_in[3] = size & 255;

		xferp->data_in[4] = (logical_block_size >> 24) & 255;
		xferp->data_in[5] = (logical_block_size >> 16) & 255;
		xferp->data_in[6] = (logical_block_size >> 8) & 255;
		xferp->data_in[7] = logical_block_size & 255;

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_MODE_SENSE:
		debug("MODE_SENSE");

		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: check more cmd fields  */

		retlen = xferp->cmd[4];

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, retlen);

		/*  TODO: fill in page codes and whatever...  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_READ:
		debug("READ");

		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*
		 *  bits 4..0 of cmd[1], and cmd[2] and cmd[3] hold the
		 *  logical block address.
		 *
		 *  cmd[4] holds the number of logical blocks to transfer.
		 *  (special case if the value is 0, actually means 256.)
		 */
		ofs = (xferp->cmd[1] & 0x1f) << 16 +
		      (xferp->cmd[2]) << 8 + xferp->cmd[3];
		retlen = xferp->cmd[4];
		if (retlen == 0)
			retlen = 256;

		size = retlen * logical_block_size;
		ofs *= logical_block_size;

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, size);

		diskimage_access(disk_id, 0, ofs, xferp->data_in, size);
		/*  TODO: how about return code?  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_REQUEST_SENSE:
	case 0x15:
	case 0x1b:
	case 0x1e:
	case SCSICDROM_READ_SUBCHANNEL:
	case SCSICDROM_READ_TOC:
		fatal("[ SCSI 0x%02x: TODO ]\n", xferp->cmd[0]);
		break;

	default:
		fatal("unimplemented SCSI command 0x%02x\n", xferp->cmd[0]);
		exit(1);
	}
	debug(" ]\n");

	return 1;
}


/*
 *  diskimage_access():
 *
 *  Read from or write to a disk image.
 *
 *  Returns 1 if the access completed successfully, 0 otherwise.
 */
int diskimage_access(int disk_id, int writeflag, off_t offset, unsigned char *buf, size_t len)
{
	int len_done;

	if (disk_id >= n_diskimages || diskimages[disk_id]==NULL) {
		fatal("trying to access a non-existant disk image (%i)\n", disk_id);
		exit(1);
	}

	fseek(diskimages[disk_id]->f, offset, SEEK_SET);

	if (writeflag) {
		if (!diskimages[disk_id]->writable)
			return 0;

		len_done = fwrite(buf, 1, len, diskimages[disk_id]->f);
	} else {
		len_done = fread(buf, 1, len, diskimages[disk_id]->f);
		if (len_done < len)
			memset(buf + len_done, 0, len-len_done);
	}

	/*  Warn about non-complete data transfers:  */
	if (len_done != len) {
		fatal("diskimage_access(): disk_id %i, transfer not completed. len=%i, len_done=%i\n",
		    disk_id, len, len_done);
		return 0;
	}

	return 1;
}


/*
 *  diskimage_add():
 *
 *  Add a disk image.  fname is the filename of the disk image.
 *  If the filename begins with "C:", it is treated as a CDROM
 *  instead of a normal disk (and the "C:" part is skipped).
 *
 *  Returns an integer >= 0 identifying the disk image.
 */
int diskimage_add(char *fname)
{
	int id;
	FILE *f;

	if (n_diskimages >= MAX_DISKIMAGES) {
		fprintf(stderr, "too many disk images\n");
		exit(1);
	}

	id = n_diskimages;

	diskimages[id] = malloc(sizeof(struct diskimage));
	if (diskimages[id] == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(diskimages[id], 0, sizeof(struct diskimage));

	diskimages[id]->is_a_cdrom = 0;

	diskimages[id]->fname = malloc(strlen(fname) + 1);
	if (diskimages[id]->fname == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/*  If the filename begins with "C:" then it is a CD-ROM image:  */
	if (strlen(fname) > 2 && strncasecmp(fname, "C:", 2) == 0) {
		diskimages[id]->is_a_cdrom = 1;
		fname += 2;
	}

	strcpy(diskimages[id]->fname, fname);

	/*  Measure total_size:  */
	f = fopen(fname, "r");
	if (f == NULL) {
		perror(fname);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	diskimages[id]->total_size = ftell(f);
	fclose(f);

	diskimages[id]->writable = access(fname, W_OK) == 0? 1 : 0;

	/*
	 *  Make an intelligent guess that filenames ending with .iso
	 *  mean CD-ROM images.
	 */
	if (strlen(diskimages[id]->fname) > 4 &&
	    strcasecmp(diskimages[id]->fname + strlen(diskimages[id]->fname) - 4, ".iso") == 0)
		diskimages[id]->is_a_cdrom = 1;

	if (diskimages[id]->is_a_cdrom)
		diskimages[id]->writable = 0;

	diskimages[id]->f = fopen(fname, diskimages[id]->writable? "r+" : "r");
	if (diskimages[id]->f == NULL) {
		perror(fname);
		exit(1);
	}

	n_diskimages ++;

	return id;
}


/*
 *  diskimage_dump_info():
 *
 *  Debug dump of all diskimages that are loaded.
 *
 *  TODO:  The word 'adding' isn't really correct, as all diskimages
 *         are actually already added when this function is called.
 */
void diskimage_dump_info(void)
{
	int i;

	for (i=0; i<n_diskimages; i++) {
		debug("adding diskimage %i: '%s', %s, %li bytes (%s%li sectors)\n",
		    i, diskimages[i]->fname,
		    diskimages[i]->writable? "read/write" : "read-only",
		    (long int) diskimages[i]->total_size,
		    diskimages[i]->is_a_cdrom? "CD-ROM, " : "",
		    (long int) (diskimages[i]->total_size / 512));
	}
}


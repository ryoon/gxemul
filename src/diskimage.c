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
 *  $Id: diskimage.c,v 1.16 2004-04-12 08:19:15 debug Exp $
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
	int		is_boot_device;

	int		rpms;
	int		ncyls;

	FILE		*f;
};


extern int emulation_type;


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
		fprintf(stderr, "scsi_transfer_allocbuf(): out of memory trying to allocate %li bytes\n", (long)want_len);
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
	if (disk_id < 0 || disk_id >= MAX_DISKIMAGES || diskimages[disk_id]==NULL)
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
	if (disk_id < 0 || disk_id >= MAX_DISKIMAGES || diskimages[disk_id]==NULL)
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
 *  Returns:
 *	2 if the command expects data from the DATA_OUT phase,
 *	1 if otherwise ok,
 *	0 on error.
 */
int diskimage_scsicommand(int disk_id, struct scsi_transfer *xferp)
{
	int retlen;
	int64_t size, ofs;
	int pagecode;

	if (disk_id < 0 || disk_id >= MAX_DISKIMAGES || diskimages[disk_id]==NULL)
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
		if (xferp->cmd[1] != 0x00)
			fatal("WARNING: TEST_UNIT_READY with cmd[1]=0x%02x not yet implemented\n");

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
		xferp->data_in[4] = 0x2c - 4;	/*  Additional length  */
		xferp->data_in[6] = 0x04;	/*  ACKREQQ  */
		xferp->data_in[7] = 0x60;	/*  WBus32, WBus16  */

		/*  These must be padded with spaces:  */
		memcpy(xferp->data_in+8,  "FAKE    ", 8);
		memcpy(xferp->data_in+16, "DISK            ", 16);
		memcpy(xferp->data_in+32, "V0.0", 4);

		if (emulation_type == EMULTYPE_DEC) {
			/*  DEC, RZxx:  */
			memcpy(xferp->data_in+8,  "DEC     ", 8);
			memcpy(xferp->data_in+16, "RZ58            ", 16);
		}

		/*  Some data is different for CD-ROM drives:  */
		if (diskimages[disk_id]->is_a_cdrom) {
			xferp->data_in[0] = 0x05;	/*  0x05 = CD-ROM  */
			xferp->data_in[1] = 0x80;	/*  0x80 = removable  */
			memcpy(xferp->data_in+16, "CD-ROM          ", 16);

			if (emulation_type == EMULTYPE_DEC) {
				/*  SONY, CD-ROM:  */
				memcpy(xferp->data_in+8,  "SONY    ", 8);
			}
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

		retlen = xferp->cmd[4];

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, retlen);

		pagecode = xferp->cmd[2] & 0x3f;

		/*  4 or 8 bytes of header  */
		xferp->data_in[0] = retlen;		/*  ?  */
		xferp->data_in[3] = 8 * 1;		/*  ?: 1 page  */

		/*  descriptors, 8 bytes (each)  */

		/*  page, n bytes (each)  */
		switch (pagecode) {
		case 1:		/*  read-write error recovery page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 10;
			break;
		case 3:		/*  format device page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 22;

			/*  10,11 = sectors per track  */
			xferp->data_in[12 + 10] = 0;
			xferp->data_in[12 + 11] = 1;	/*  TODO  */

			/*  12,13 = physical sector size  */
			xferp->data_in[12 + 12] = (logical_block_size >> 8) & 255;
			xferp->data_in[12 + 13] = logical_block_size & 255;
			break;
		case 4:		/*  rigid disk geometry page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 22;
			xferp->data_in[12 + 2] = (diskimages[disk_id]->ncyls >> 16) & 255;
			xferp->data_in[12 + 3] = (diskimages[disk_id]->ncyls >> 8) & 255;
			xferp->data_in[12 + 4] = diskimages[disk_id]->ncyls & 255;
			xferp->data_in[12 + 5] = 15;	/*  nr of heads  */

			xferp->data_in[12 + 20] = (diskimages[disk_id]->rpms >> 8) & 255;
			xferp->data_in[12 + 21] = diskimages[disk_id]->rpms & 255;
			break;
		case 5:		/*  flexible disk page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 0x1e;

			/*  2,3 = transfer rate  */
			xferp->data_in[12 + 2] = ((5000) >> 8) & 255;
			xferp->data_in[12 + 3] = (5000) & 255;

			xferp->data_in[12 + 4] = 2;	/*  nr of heads  */
			xferp->data_in[12 + 5] = 18;	/*  sectors per track  */

			/*  6,7 = data bytes per sector  */
			xferp->data_in[12 + 6] = (logical_block_size >> 8) & 255;
			xferp->data_in[12 + 7] = logical_block_size & 255;

			xferp->data_in[12 + 8] = (diskimages[disk_id]->ncyls >> 8) & 255;
			xferp->data_in[12 + 9] = diskimages[disk_id]->ncyls & 255;

			xferp->data_in[12 + 28] = (diskimages[disk_id]->rpms >> 8) & 255;
			xferp->data_in[12 + 29] = diskimages[disk_id]->rpms & 255;
			break;
		default:
			fatal("MODE_SENSE for page %i is not yet implemented!!!\n", pagecode);
		}


		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_READ:
	case SCSICMD_READ_10:
		debug("READ");

		if (xferp->cmd[0] == SCSICMD_READ) {
			if (xferp->cmd_len != 6)
				debug(" (weird len=%i)", xferp->cmd_len);

			/*
			 *  bits 4..0 of cmd[1], and cmd[2] and cmd[3] hold the
			 *  logical block address.
			 *
			 *  cmd[4] holds the number of logical blocks to transfer.
			 *  (special case if the value is 0, actually means 256.)
			 */
			ofs = ((xferp->cmd[1] & 0x1f) << 16) +
			      (xferp->cmd[2] << 8) + xferp->cmd[3];
			retlen = xferp->cmd[4];
			if (retlen == 0)
				retlen = 256;
		} else {
			if (xferp->cmd_len != 10)
				debug(" (weird len=%i)", xferp->cmd_len);

			/*
			 *  cmd[2..5] hold the logical block address.
			 *  cmd[7..8] holds the number of logical blocks to transfer.
			 *  (special case if the value is 0, actually means 65536.)
			 */
			ofs = (xferp->cmd[2] << 24) + (xferp->cmd[3] << 16) +
			      (xferp->cmd[4] << 8) + xferp->cmd[5];
			retlen = (xferp->cmd[7] << 8) + xferp->cmd[8];
			if (retlen == 0)
				retlen = 65536;
		}

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

	case SCSICMD_WRITE:
	case SCSICMD_WRITE_10:
		debug("WRITE");

		if (xferp->cmd[0] == SCSICMD_WRITE) {
			if (xferp->cmd_len != 6)
				debug(" (weird len=%i)", xferp->cmd_len);

			/*
			 *  bits 4..0 of cmd[1], and cmd[2] and cmd[3] hold the
			 *  logical block address.
			 *
			 *  cmd[4] holds the number of logical blocks to transfer.
			 *  (special case if the value is 0, actually means 256.)
			 */
			ofs = ((xferp->cmd[1] & 0x1f) << 16) +
			      (xferp->cmd[2] << 8) + xferp->cmd[3];
			retlen = xferp->cmd[4];
			if (retlen == 0)
				retlen = 256;
		} else {
			if (xferp->cmd_len != 10)
				debug(" (weird len=%i)", xferp->cmd_len);

			/*
			 *  cmd[2..5] hold the logical block address.
			 *  cmd[7..8] holds the number of logical blocks to transfer.
			 *  (special case if the value is 0, actually means 65536.)
			 */
			ofs = (xferp->cmd[2] << 24) + (xferp->cmd[3] << 16) +
			      (xferp->cmd[4] << 8) + xferp->cmd[5];
			retlen = (xferp->cmd[7] << 8) + xferp->cmd[8];
			if (retlen == 0)
				retlen = 65536;
		}

		size = retlen * logical_block_size;
		ofs *= logical_block_size;

		if (xferp->data_out == NULL) {
			debug(", data_out == NULL, wanting %i bytes, \n\n", (int)size);
			xferp->data_out_len = size;
			return 2;
		}

		debug(", data_out != NULL, OK :-)");

		diskimage_access(disk_id, 1, ofs, xferp->data_out, size);
		/*  TODO: how about return code?  */

		fsync(fileno(diskimages[disk_id]->f));

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_SYNCHRONIZE_CACHE:
		debug("SYNCHRONIZE_CACHE");

		if (xferp->cmd_len != 10)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: actualy care about cmd[]  */
		fsync(fileno(diskimages[disk_id]->f));

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_START_STOP_UNIT:
		debug("START_STOP_UNIT");

		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: actualy care about cmd[]  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_REQUEST_SENSE:
		debug("REQUEST_SENSE");

		retlen = xferp->cmd[4];

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if (xferp->cmd[1] != 0x00)
			fatal("WARNING: REQUEST_SENSE with cmd[1]=0x%02x not yet implemented\n");

		if (retlen < 18) {
			fatal("WARNING: SCSI request sense len=%i, <18!\n", retlen);
			retlen = 18;
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, retlen);

		xferp->data_in[0] = 0x80 + 0x70;	/*  0x80 = valid, 0x70 = "current errors"  */
		/*  TODO  */
		xferp->data_in[7] = retlen - 7;		/*  additional sense length  */
		/*  TODO  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICDROM_READ_SUBCHANNEL:
	case SCSICDROM_READ_TOC:
		fatal("[ SCSI 0x%02x: TODO ]\n", xferp->cmd[0]);

		retlen = xferp->cmd[4];

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in, retlen);

		/*  TODO  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

		break;

	case SCSICMD_MODE_SELECT:
	case 0x1e:
		fatal("[ SCSI 0x%02x: TODO ]\n", xferp->cmd[0]);

		/*  TODO  */

		/*  Return status and message:  */
		scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1);
		xferp->status[0] = 0x00;
		scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
		xferp->msg_in[0] = 0x00;

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

	if (disk_id >= MAX_DISKIMAGES || diskimages[disk_id]==NULL) {
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
 *  The filename may be prefixed with one or more modifiers, followed
 *  by a colon.
 *
 *	b	specifies that this is the boot device
 *	c	CD-ROM (instead of normal SCSI DISK)
 *	d	SCSI DISK (this is the default)
 *	r       read-only (don't allow changes to the file)
 *	0-7	force a specific SCSI ID number
 *
 *  Returns an integer >= 0 identifying the disk image.
 */
int diskimage_add(char *fname)
{
	int id;
	FILE *f;
	char *cp;
	int prefix_b = 0;
	int prefix_c = 0;
	int prefix_d = 0;
	int prefix_id = -1;
	int prefix_r = 0;

	if (fname == NULL) {
		fprintf(stderr, "diskimage_add(): NULL ptr\n");
		return 0;
	}

	/*  Get prefix from fname:  */
	cp = strchr(fname, ':');
	if (cp != NULL) {
		while (fname <= cp) {
			char c = *fname++;
			switch (c) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				prefix_id = c - '0';
				break;
			case 'b':
				prefix_b = 1;
				break;
			case 'c':
				prefix_c = 1;
				break;
			case 'd':
				prefix_d = 1;
				break;
			case 'r':
				prefix_r = 1;
				break;
			case ':':
				break;
			default:
				fprintf(stderr, "diskimage_add(): invalid prefix char '%c'\n", c);
				exit(1);
			}
		}
	}

	/*  Calculate which ID to use:  */
	if (prefix_id == -1) {
		/*  Find first free ID:  */
		for (id = 0; id < MAX_DISKIMAGES; id++) {
			if (diskimages[id] == NULL)
				break;
		}

		if (id >= MAX_DISKIMAGES) {
			fprintf(stderr, "too many disk images\n");
			exit(1);
		}
	} else {
		id = prefix_id;
		if (id < 0 || id >= MAX_DISKIMAGES) {
			fprintf(stderr, "invalid id\n");
			exit(1);
		}
		if (diskimages[id] != NULL) {
			fprintf(stderr, "disk image id %i already in use\n", id);
			exit(1);
		}
	}


	diskimages[id] = malloc(sizeof(struct diskimage));
	if (diskimages[id] == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(diskimages[id], 0, sizeof(struct diskimage));

	diskimages[id]->fname = malloc(strlen(fname) + 1);
	if (diskimages[id]->fname == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	strcpy(diskimages[id]->fname, fname);


	/*
	 *  Is this a CD-ROM or a normal disk?
	 *
	 *  An intelligent guess would be that filenames ending with .iso
	 *  are CD-ROM images.
	 */
	if (prefix_c ||
	    ((strlen(diskimages[id]->fname) > 4 &&
	    strcasecmp(diskimages[id]->fname + strlen(diskimages[id]->fname) - 4, ".iso") == 0)
	    && !prefix_d)
	   )
		diskimages[id]->is_a_cdrom = 1;

	/*  Measure total_size:  */
	f = fopen(fname, "r");
	if (f == NULL) {
		perror(fname);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	diskimages[id]->total_size = ftell(f);
	fclose(f);

	diskimages[id]->ncyls = diskimages[id]->total_size / 1048576;
	diskimages[id]->rpms = 3600;

	if (prefix_b)
		diskimages[id]->is_boot_device = 1;

	diskimages[id]->writable = access(fname, W_OK) == 0? 1 : 0;

	if (diskimages[id]->is_a_cdrom || prefix_r)
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
 *  diskimage_bootdev():
 *
 *  Returns the disk id (0..7) of the device which we're booting from.
 */
int diskimage_bootdev(void)
{
	int i;
	int first_dev = -1;
	int bootdev = -1;

	for (i=0; i<MAX_DISKIMAGES; i++) {
		if (diskimages[i] != NULL && first_dev < 0)
			first_dev = i;

		if (diskimages[i] != NULL && diskimages[i]->is_boot_device) {
			if (bootdev == -1)
				bootdev = i;
			else {
				fprintf(stderr, "more than one boot device? id %i and id %i\n",
				    bootdev, i);
			}
		}
	}

	if (bootdev < 0) {
		bootdev = first_dev;

		if (bootdev < 0)
			bootdev = 0;	/*  Just accept that there's no boot device.  */
		else
			debug("No disk marked as boot device; booting from id %i\n", bootdev);
	}

	return bootdev;
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

	for (i=0; i<MAX_DISKIMAGES; i++)
		if (diskimages[i] != NULL) {
			debug("adding diskimage id=%i: '%s', %s, %li bytes (%s%li sectors)%s\n",
			    i, diskimages[i]->fname,
			    diskimages[i]->writable? "read/write" : "read-only",
			    (long int) diskimages[i]->total_size,
			    diskimages[i]->is_a_cdrom? "CD-ROM, " : "DISK, ",
			    (long int) (diskimages[i]->total_size / 512),
			    diskimages[i]->is_boot_device? ", BOOT DEVICE" : "");
		}
}


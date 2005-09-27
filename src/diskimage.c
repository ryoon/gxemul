/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: diskimage.c,v 1.97 2005-09-27 23:18:30 debug Exp $
 *
 *  Disk image support.
 *
 *  TODO:  There's probably a bug in the tape support:
 *         Let's say there are 10240 bytes left in a file, and 10240
 *         bytes are read. Then feof() is not true yet (?), so the next
 *         read will also return 10240 bytes (but all zeroes), and then after
 *         that return feof (which results in a filemark).  This is probably
 *         trivial to fix, but I don't feel like it right now.
 *
 *  TODO:  diskimage_remove()? This would be useful for floppies in PC-style
 *	   machines, where disks may need to be swapped during boot etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cpu.h"
#include "diskimage.h"
#include "machine.h"
#include "misc.h"


extern int quiet_mode;
extern int single_step;

static char *diskimage_types[] = DISKIMAGE_TYPES;

static struct scsi_transfer *first_free_scsi_transfer_alloc = NULL;


/**************************************************************************/

/*
 *  my_fseek():
 *
 *  A helper function, like fseek() but takes off_t.  If the system has
 *  fseeko, then that is used. Otherwise I try to fake off_t offsets here.
 *
 *  The correct position is reached by seeking 2 billion bytes at a time
 *  (or less).  Note: This method is only used for SEEK_SET, for SEEK_CUR
 *  and SEEK_END, normal fseek() is used!
 *
 *  TODO: It seemed to work on Linux/i386, but not on Solaris/sparc (?).
 *  Anyway, most modern systems have fseeko(), so it shouldn't be a problem.
 */
static int my_fseek(FILE *f, off_t offset, int whence)
{
#ifdef HACK_FSEEKO
	if (whence == SEEK_SET) {
		int res = 0;
		off_t curoff = 0;
		off_t cur_step;

		fseek(f, 0, SEEK_SET);
		while (curoff < offset) {
			/*  How far to seek?  */
			cur_step = offset - curoff;
			if (cur_step > 2000000000)
				cur_step = 2000000000;
			res = fseek(f, cur_step, SEEK_CUR);
			if (res)
				return res;
			curoff += cur_step;
		}
		return 0;
	} else
		return fseek(f, offset, whence);
#else
	return fseeko(f, offset, whence);
#endif
}


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

	if (first_free_scsi_transfer_alloc != NULL) {
		p = first_free_scsi_transfer_alloc;
		first_free_scsi_transfer_alloc = p->next_free;
	} else {
		p = malloc(sizeof(struct scsi_transfer));
		if (p == NULL) {
			fprintf(stderr, "scsi_transfer_alloc(): out "
			    "of memory\n");
			exit(1);
		}
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

	p->next_free = first_free_scsi_transfer_alloc;
	first_free_scsi_transfer_alloc = p;
}


/*
 *  scsi_transfer_allocbuf():
 *
 *  Helper function, used by diskimage_scsicommand(), and SCSI controller
 *  devices.  Example of usage:
 *
 *	scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1);
 */
void scsi_transfer_allocbuf(size_t *lenp, unsigned char **pp, size_t want_len,
	int clearflag)
{
	unsigned char *p = (*pp);

	if (p != NULL) {
		printf("WARNING! scsi_transfer_allocbuf(): old pointer "
		    "was not NULL, freeing it now\n");
		free(p);
	}

	(*lenp) = want_len;
	if ((p = malloc(want_len)) == NULL) {
		fprintf(stderr, "scsi_transfer_allocbuf(): out of "
		    "memory trying to allocate %li bytes\n", (long)want_len);
		exit(1);
	}

	if (clearflag)
		memset(p, 0, want_len);

	(*pp) = p;
}


/**************************************************************************/


/*
 *  diskimage_exist():
 *
 *  Returns 1 if the specified disk id (for a specific type) exists, 0
 *  otherwise.
 */
int diskimage_exist(struct machine *machine, int id, int type)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id)
			return 1;
		d = d->next;
	}
	return 0;
}


/*
 *  diskimage_recalc_size():
 *
 *  Recalculate a disk's size by stat()-ing it.
 *  d is assumed to be non-NULL.
 */
static void diskimage_recalc_size(struct diskimage *d)
{
	struct stat st;
	int res;
	off_t size = 0;

	res = stat(d->fname, &st);
	if (res) {
		fprintf(stderr, "[ diskimage_recalc_size(): could not stat "
		    "'%s' ]\n", d->fname);
		return;
	}

	size = st.st_size;

	/*
	 *  TODO:  CD-ROM devices, such as /dev/cd0c, how can one
	 *  check how much data is on that cd-rom without reading it?
	 *  For now, assume some large number, hopefully it will be
	 *  enough to hold any cd-rom image.
	 */
	if (d->is_a_cdrom && size == 0)
		size = 762048000;

	d->total_size = size;
	d->ncyls = d->total_size / 1048576;

	/*  TODO: There is a mismatch between d->ncyls and d->cylinders,
	    SCSI-based stuff usually doesn't care.  TODO: Fix this.  */
}


/*
 *  diskimage_getsize():
 *
 *  Returns -1 if the specified disk id/type does not exists, otherwise
 *  the size of the disk image is returned.
 */
int64_t diskimage_getsize(struct machine *machine, int id, int type)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id)
			return d->total_size;
		d = d->next;
	}
	return -1;
}


/*
 *  diskimage_getchs():
 *
 *  Returns the current CHS values of a disk image.
 */
void diskimage_getchs(struct machine *machine, int id, int type,
	int *c, int *h, int *s)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id) {
			*c = d->cylinders;
			*h = d->heads;
			*s = d->sectors_per_track;
			return;
		}
		d = d->next;
	}
	fatal("diskimage_getchs(): disk id %i (type %i) not found?\n",
	    id, diskimage_types[type]);
	exit(1);
}


/*
 *  diskimage__return_default_status_and_message():
 *
 *  Set the status and msg_in parts of a scsi_transfer struct
 *  to default values (msg_in = 0x00, status = 0x00).
 */
static void diskimage__return_default_status_and_message(
	struct scsi_transfer *xferp)
{
	scsi_transfer_allocbuf(&xferp->status_len, &xferp->status, 1, 0);
	xferp->status[0] = 0x00;
	scsi_transfer_allocbuf(&xferp->msg_in_len, &xferp->msg_in, 1, 0);
	xferp->msg_in[0] = 0x00;
}


/*
 *  diskimage__switch_tape():
 *
 *  Used by the SPACE command.  (d is assumed to be non-NULL.)
 */
static void diskimage__switch_tape(struct diskimage *d)
{
	char tmpfname[1000];

	snprintf(tmpfname, sizeof(tmpfname), "%s.%i",
	    d->fname, d->tape_filenr);
	tmpfname[sizeof(tmpfname)-1] = '\0';

	if (d->f != NULL)
		fclose(d->f);

	d->f = fopen(tmpfname, d->writable? "r+" : "r");
	if (d->f == NULL) {
		fprintf(stderr, "[ diskimage__switch_tape(): could not "
		    "(re)open '%s' ]\n", tmpfname);
		/*  TODO: return error  */
	}
	d->tape_offset = 0;
}


/*
 *  diskimage_access__cdrom():
 *
 *  This is a special-case function, called from diskimage__internal_access().
 *  On my FreeBSD 4.9 system, the cdrom device /dev/cd0c seems to not be able
 *  to handle something like "fseek(512); fread(512);" but it handles
 *  "fseek(2048); fread(512);" just fine.  So, if diskimage__internal_access()
 *  fails in reading a block of data, this function is called as an attempt to
 *  align reads at 2048-byte sectors instead.
 *
 *  (Ugly hack.  TODO: how to solve this cleanly?)
 *
 *  NOTE:  Returns the number of bytes read, 0 if nothing was successfully
 *  read. (These are not the same as diskimage_access()).
 */
#define	CDROM_SECTOR_SIZE	2048
static size_t diskimage_access__cdrom(struct diskimage *d, off_t offset,
	unsigned char *buf, size_t len)
{
	off_t aligned_offset;
	size_t bytes_read, total_copied = 0;
	unsigned char cdrom_buf[CDROM_SECTOR_SIZE];
	off_t buf_ofs, i = 0;

	/*  printf("diskimage_access__cdrom(): offset=0x%llx size=%lli\n",
	    (long long)offset, (long long)len);  */

	aligned_offset = (offset / CDROM_SECTOR_SIZE) * CDROM_SECTOR_SIZE;
	my_fseek(d->f, aligned_offset, SEEK_SET);

	while (len != 0) {
		bytes_read = fread(cdrom_buf, 1, CDROM_SECTOR_SIZE, d->f);
		if (bytes_read != CDROM_SECTOR_SIZE)
			return 0;

		/*  Copy (part of) cdrom_buf into buf:  */
		buf_ofs = offset - aligned_offset;
		while (buf_ofs < CDROM_SECTOR_SIZE && len != 0) {
			buf[i ++] = cdrom_buf[buf_ofs ++];
			total_copied ++;
			len --;
		}

		aligned_offset += CDROM_SECTOR_SIZE;
		offset = aligned_offset;
	}

	return total_copied;
}


/*
 *  diskimage__internal_access():
 *
 *  Read from or write to a struct diskimage.
 *
 *  Returns 1 if the access completed successfully, 0 otherwise.
 */
static int diskimage__internal_access(struct diskimage *d, int writeflag,
	off_t offset, unsigned char *buf, size_t len)
{
	ssize_t lendone;
	int res;

	if (buf == NULL) {
		fprintf(stderr, "diskimage__internal_access(): buf = NULL\n");
		exit(1);
	}
	if (len == 0)
		return 1;
	if (d->f == NULL)
		return 0;

	res = my_fseek(d->f, offset, SEEK_SET);
	if (res != 0) {
		fatal("[ diskimage__internal_access(): fseek() failed on "
		    "disk id %i \n", d->id);
		return 0;
	}

	if (writeflag) {
		if (!d->writable)
			return 0;

		lendone = fwrite(buf, 1, len, d->f);
	} else {
		/*
		 *  Special case for CD-ROMs. Actually, this is not needed
		 *  for .iso images, only for physical CDROMS on some OSes,
		 *  such as FreeBSD.
		 */
		if (d->is_a_cdrom)
			lendone = diskimage_access__cdrom(d, offset, buf, len);
		else
			lendone = fread(buf, 1, len, d->f);

		if (lendone < (ssize_t)len)
			memset(buf + lendone, 0, len - lendone);
	}

	/*  Warn about non-complete data transfers:  */
	if (lendone != (ssize_t)len) {
		fatal("[ diskimage__internal_access(): disk_id %i, offset %lli"
		    ", transfer not completed. len=%i, len_done=%i ]\n",
		    d->id, (long long)offset, (int)len, (int)lendone);
		return 0;
	}

	return 1;
}


/*
 *  diskimage_scsicommand():
 *
 *  Perform a SCSI command on a disk image.
 *
 *  The xferp points to a scsi_transfer struct, containing msg_out, command,
 *  and data_out coming from the SCSI controller device.  This function
 *  interprets the command, and (if necessary) creates responses in
 *  data_in, msg_in, and status.
 *
 *  Returns:
 *	2 if the command expects data from the DATA_OUT phase,
 *	1 if otherwise ok,
 *	0 on error.
 */
int diskimage_scsicommand(struct cpu *cpu, int id, int type,
	struct scsi_transfer *xferp)
{
	int retlen, i;
	uint64_t size;
	int64_t ofs;
	int pagecode;
	struct machine *machine = cpu->machine;
	struct diskimage *d;

	if (machine == NULL) {
		fatal("[ diskimage_scsicommand(): machine == NULL ]\n");
		return 0;
	}

	d = machine->first_diskimage;
	while (d != NULL) {
		if (d->type == type && d->id == id)
			break;
		d = d->next;
	}
	if (d == NULL) {
		fprintf(stderr, "[ diskimage_scsicommand(): %s "
		    " id %i not connected? ]\n", diskimage_types[type], id);
	}

	if (xferp->cmd == NULL) {
		fatal("[ diskimage_scsicommand(): cmd == NULL ]\n");
		return 0;
	}

	if (xferp->cmd_len < 1) {
		fatal("[ diskimage_scsicommand(): cmd_len == %i ]\n",
		    xferp->cmd_len);
		return 0;
	}

	debug("[ diskimage_scsicommand(id=%i) cmd=0x%02x: ",
	    id, xferp->cmd[0]);

#if 0
	fatal("[ diskimage_scsicommand(id=%i) cmd=0x%02x len=%i:",
	    id, xferp->cmd[0], xferp->cmd_len);
	for (i=0; i<xferp->cmd_len; i++)
		fatal(" %02x", xferp->cmd[i]);
	fatal("\n");
if (xferp->cmd_len > 7 && xferp->cmd[5] == 0x11)
	single_step = 1;
#endif

#if 0
{
	static FILE *f = NULL;
	if (f == NULL)
		f = fopen("scsi_log.txt", "w"); 
	if (f != NULL) {
		int i;
		fprintf(f, "id=%i cmd =", id);
		for (i=0; i<xferp->cmd_len; i++)
			fprintf(f, " %02x", xferp->cmd[i]);
		fprintf(f, "\n");
		fflush(f);
	}
}
#endif

	switch (xferp->cmd[0]) {

	case SCSICMD_TEST_UNIT_READY:
		debug("TEST_UNIT_READY");
		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if (xferp->cmd[1] != 0x00)
			fatal("WARNING: TEST_UNIT_READY with cmd[1]=0x%02x"
			    " not yet implemented\n", (int)xferp->cmd[1]);

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_INQUIRY:
		debug("INQUIRY");
		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);
		if (xferp->cmd[1] != 0x00) {
			debug("WARNING: INQUIRY with cmd[1]=0x%02x not yet "
			    "implemented\n", (int)xferp->cmd[1]);

			break;
		}

		/*  Return values:  */
		retlen = xferp->cmd[4];
		if (retlen < 36) {
			fatal("WARNING: SCSI inquiry len=%i, <36!\n", retlen);
			retlen = 36;
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in,
		    retlen, 1);
		xferp->data_in[0] = 0x00;  /*  0x00 = Direct-access disk  */
		xferp->data_in[1] = 0x00;  /*  0x00 = non-removable  */
		xferp->data_in[2] = 0x02;  /*  SCSI-2  */
#if 0
xferp->data_in[3] = 0x02;	/*  Response data format = SCSI-2  */
#endif
		xferp->data_in[4] = retlen - 4;	/*  Additional length  */
xferp->data_in[4] = 0x2c - 4;	/*  Additional length  */
		xferp->data_in[6] = 0x04;  /*  ACKREQQ  */
		xferp->data_in[7] = 0x60;  /*  WBus32, WBus16  */

		/*  These must be padded with spaces:  */
		memcpy(xferp->data_in+8,  "FAKE    ", 8);
		memcpy(xferp->data_in+16, "DISK            ", 16);
		memcpy(xferp->data_in+32, "V0.0", 4);

		/*
		 *  Some Ultrix kernels want specific responses from
		 *  the drives.
		 */

		if (machine->machine_type == MACHINE_DEC) {
			/*  DEC, RZ25 (rev 0900) = 832527 sectors  */
			/*  DEC, RZ58 (rev 2000) = 2698061 sectors  */
			memcpy(xferp->data_in+8,  "DEC     ", 8);
			memcpy(xferp->data_in+16, "RZ58     (C) DEC", 16);
			memcpy(xferp->data_in+32, "2000", 4);
		}

		/*  Some data is different for CD-ROM drives:  */
		if (d->is_a_cdrom) {
			xferp->data_in[0] = 0x05;  /*  0x05 = CD-ROM  */
			xferp->data_in[1] = 0x80;  /*  0x80 = removable  */
			memcpy(xferp->data_in+16, "CD-ROM          ", 16);

			if (machine->machine_type == MACHINE_DEC) {
				/*  SONY, CD-ROM:  */
				memcpy(xferp->data_in+8, "SONY    ", 8);
				memcpy(xferp->data_in+16,
				    "CD-ROM          ", 16);

				/*  ... or perhaps this:  */
				memcpy(xferp->data_in+8, "DEC     ", 8);
				memcpy(xferp->data_in+16,
				    "RRD42   (C) DEC ", 16);
				memcpy(xferp->data_in+32, "4.5d", 4);
			} else {
				/*  NEC, CD-ROM:  */
				memcpy(xferp->data_in+8, "NEC     ", 8);
				memcpy(xferp->data_in+16,
				    "CD-ROM CDR-210P ", 16);
				memcpy(xferp->data_in+32, "1.0 ", 4);
			}
		}

		/*  Data for tape devices:  */
		if (d->is_a_tape) {
			xferp->data_in[0] = 0x01;  /*  0x01 = tape  */
			xferp->data_in[1] = 0x80;  /*  0x80 = removable  */
			memcpy(xferp->data_in+16, "TAPE            ", 16);

			if (machine->machine_type == MACHINE_DEC) {
				/*
				 *  TODO:  find out if these are correct.
				 *
				 *  The name might be TZK10, TSZ07, or TLZ04,
				 *  or something completely different.
				 */
				memcpy(xferp->data_in+8, "DEC     ", 8);
				memcpy(xferp->data_in+16,
				    "TK50     (C) DEC", 16);
				memcpy(xferp->data_in+32, "2000", 4);
			}
		}

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSIBLOCKCMD_READ_CAPACITY:
		debug("READ_CAPACITY");

		if (xferp->cmd_len != 10)
			fatal(" [ weird READ_CAPACITY len=%i, should be 10 ] ",
			    xferp->cmd_len);
		else {
			if (xferp->cmd[8] & 1) {
				/*  Partial Medium Indicator bit...  TODO  */
				fatal("WARNING: READ_CAPACITY with PMI bit"
				    " set not yet implemented\n");
			}
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in,
		    8, 1);

		diskimage_recalc_size(d);

		size = d->total_size / d->logical_block_size;
		if (d->total_size & (d->logical_block_size-1))
			size ++;

		xferp->data_in[0] = (size >> 24) & 255;
		xferp->data_in[1] = (size >> 16) & 255;
		xferp->data_in[2] = (size >> 8) & 255;
		xferp->data_in[3] = size & 255;

		xferp->data_in[4] = (d->logical_block_size >> 24) & 255;
		xferp->data_in[5] = (d->logical_block_size >> 16) & 255;
		xferp->data_in[6] = (d->logical_block_size >> 8) & 255;
		xferp->data_in[7] = d->logical_block_size & 255;

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_MODE_SENSE:
		debug("MODE_SENSE");

		if (xferp->cmd_len != 6)
			fatal(" (unimplemented mode_sense len=%i)",
			    xferp->cmd_len);

		retlen = xferp->cmd[4];

		/*
		 *  NOTE/TODO: This code doesn't handle too short retlens
		 *  very well. A quick hack around this is that I allocate
		 *  a bit too much memory, so that nothing is actually
		 *  written outside of xferp->data_in[].
		 */

		retlen += 100;		/*  Should be enough. (Ugly.)  */

		if ((xferp->cmd[2] & 0xc0) != 0)
			fatal("WARNING: mode sense, cmd[2] = 0x%02x\n",
			    xferp->cmd[2]);

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len,
		    &xferp->data_in, retlen, 1);

		xferp->data_in_len -= 100;	/*  Restore size.  */

		pagecode = xferp->cmd[2] & 0x3f;

		debug("[ MODE SENSE id %i, pagecode=%i ]\n", id, pagecode);

		/*  4 bytes of header for 6-byte command,
		    8 bytes of header for 10-byte command.  */
		xferp->data_in[0] = retlen;	/*  0: mode data length  */
		xferp->data_in[1] = d->is_a_cdrom? 0x05 : 0x00;
				/*  1: medium type  */
		xferp->data_in[2] = 0x00;	/*  device specific
						    parameter  */
		xferp->data_in[3] = 8 * 1;	/*  block descriptor
						    length: 1 page (?)  */

		/*  TODO: update this when implementing 10-byte commands:  */
		xferp->data_in[4] = 0x00;	/*  density code  */
		xferp->data_in[5] = 0;		/*  nr of blocks, high  */
		xferp->data_in[6] = 0;		/*  nr of blocks, mid  */
		xferp->data_in[7] = 0;		/*  nr of blocks, low */
		xferp->data_in[8] = 0x00;	/*  reserved  */
		xferp->data_in[9] = (d->logical_block_size >> 16) & 255;
		xferp->data_in[10] = (d->logical_block_size >> 8) & 255;
		xferp->data_in[11] = d->logical_block_size & 255;

		diskimage__return_default_status_and_message(xferp);

		/*  descriptors, 8 bytes (each)  */

		/*  page, n bytes (each)  */
		switch (pagecode) {
		case 0:
			/*  TODO: Nothing here?  */
			break;
		case 1:		/*  read-write error recovery page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 10;
			break;
		case 3:		/*  format device page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 22;

			/*  10,11 = sectors per track  */
			xferp->data_in[12 + 10] = 0;
			xferp->data_in[12 + 11] = d->sectors_per_track;

			/*  12,13 = physical sector size  */
			xferp->data_in[12 + 12] =
			    (d->logical_block_size >> 8) & 255;
			xferp->data_in[12 + 13] = d->logical_block_size & 255;
			break;
		case 4:		/*  rigid disk geometry page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 22;
			xferp->data_in[12 + 2] = (d->ncyls >> 16) & 255;
			xferp->data_in[12 + 3] = (d->ncyls >> 8) & 255;
			xferp->data_in[12 + 4] = d->ncyls & 255;
			xferp->data_in[12 + 5] = d->heads;

			xferp->data_in[12 + 20] = (d->rpms >> 8) & 255;
			xferp->data_in[12 + 21] = d->rpms & 255;
			break;
		case 5:		/*  flexible disk page  */
			xferp->data_in[12 + 0] = pagecode;
			xferp->data_in[12 + 1] = 0x1e;

			/*  2,3 = transfer rate  */
			xferp->data_in[12 + 2] = ((5000) >> 8) & 255;
			xferp->data_in[12 + 3] = (5000) & 255;

			xferp->data_in[12 + 4] = d->heads;
			xferp->data_in[12 + 5] = d->sectors_per_track;

			/*  6,7 = data bytes per sector  */
			xferp->data_in[12 + 6] = (d->logical_block_size >> 8)
			    & 255;
			xferp->data_in[12 + 7] = d->logical_block_size & 255;

			xferp->data_in[12 + 8] = (d->ncyls >> 8) & 255;
			xferp->data_in[12 + 9] = d->ncyls & 255;

			xferp->data_in[12 + 28] = (d->rpms >> 8) & 255;
			xferp->data_in[12 + 29] = d->rpms & 255;
			break;
		default:
			fatal("[ MODE_SENSE for page %i is not yet "
			    "implemented! ]\n", pagecode);
		}

		break;

	case SCSICMD_READ:
	case SCSICMD_READ_10:
		debug("READ");

		/*
		 *  For tape devices, read data at the current position.
		 *  For disk and CDROM devices, the command bytes contain
		 *  an offset telling us where to read from the device.
		 */

		if (d->is_a_tape) {
			/*  bits 7..5 of cmd[1] are the LUN bits... TODO  */

			size = (xferp->cmd[2] << 16) +
			       (xferp->cmd[3] <<  8) +
				xferp->cmd[4];

			/*  Bit 1 of cmd[1] is the SILI bit (TODO), and
			    bit 0 is the "use fixed length" bit.  */

			if (xferp->cmd[1] & 0x01) {
				/*  Fixed block length:  */
				size *= d->logical_block_size;
			}

			if (d->filemark) {
				/*  At end of file, switch to the next
				    automagically:  */
				d->tape_filenr ++;
				diskimage__switch_tape(d);

				d->filemark = 0;
			}

			ofs = d->tape_offset;

			fatal("[ READ tape, id=%i file=%i, cmd[1]=%02x size=%i"
			    ", ofs=%lli ]\n", id, d->tape_filenr,
			    xferp->cmd[1], (int)size, (long long)ofs);
		} else {
			if (xferp->cmd[0] == SCSICMD_READ) {
				if (xferp->cmd_len != 6)
					debug(" (weird len=%i)",
					    xferp->cmd_len);

				/*
				 *  bits 4..0 of cmd[1], and cmd[2] and cmd[3]
				 *  hold the logical block address.
				 *
				 *  cmd[4] holds the number of logical blocks
				 *  to transfer. (Special case if the value is
				 *  0, actually means 256.)
				 */
				ofs = ((xferp->cmd[1] & 0x1f) << 16) +
				      (xferp->cmd[2] << 8) + xferp->cmd[3];
				retlen = xferp->cmd[4];
				if (retlen == 0)
					retlen = 256;
			} else {
				if (xferp->cmd_len != 10)
					debug(" (weird len=%i)",
					    xferp->cmd_len);

				/*
				 *  cmd[2..5] hold the logical block address.
				 *  cmd[7..8] holds the number of logical
				 *  blocks to transfer. (NOTE: If the value is
				 *  0, this means 0, not 65536. :-)
				 */
				ofs = (xferp->cmd[2] << 24) + (xferp->cmd[3]
				    << 16) + (xferp->cmd[4] << 8) +
				    xferp->cmd[5];
				retlen = (xferp->cmd[7] << 8) + xferp->cmd[8];
			}

			size = retlen * d->logical_block_size;
			ofs *= d->logical_block_size;
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in,
		    size, 0);

		debug(" READ  ofs=%lli size=%i\n", (long long)ofs, (int)size);

		diskimage__return_default_status_and_message(xferp);

		d->filemark = 0;

		/*
		 *  Failure? Then set check condition.
		 *  For tapes, error should only occur at the end of a file.
		 *
		 *  "If the logical unit encounters a filemark during
		 *   a READ command, CHECK CONDITION status shall be
		 *   returned and the filemark and valid bits shall be
		 *   set to one in the sense data. The sense key shall
		 *   be set to NO SENSE"..
		 */
		if (d->is_a_tape && d->f != NULL && feof(d->f)) {
			debug(" feof id=%i\n", id);
			xferp->status[0] = 0x02;	/*  CHECK CONDITION  */

			d->filemark = 1;
		} else
			diskimage__internal_access(d, 0, ofs,
			    xferp->data_in, size);

		if (d->is_a_tape && d->f != NULL)
			d->tape_offset = ftello(d->f);

		/*  TODO: other errors?  */
		break;

	case SCSICMD_WRITE:
	case SCSICMD_WRITE_10:
		debug("WRITE");

		/*  TODO: tape  */

		if (xferp->cmd[0] == SCSICMD_WRITE) {
			if (xferp->cmd_len != 6)
				debug(" (weird len=%i)", xferp->cmd_len);

			/*
			 *  bits 4..0 of cmd[1], and cmd[2] and cmd[3] hold the
			 *  logical block address.
			 *
			 *  cmd[4] holds the number of logical blocks to
			 *  transfer. (Special case if the value is 0, actually
			 *  means 256.)
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
			 *  cmd[7..8] holds the number of logical blocks to
			 *  transfer. (NOTE: If the value is 0 this means 0,
			 *  not 65536.)
			 */
			ofs = (xferp->cmd[2] << 24) + (xferp->cmd[3] << 16) +
			      (xferp->cmd[4] << 8) + xferp->cmd[5];
			retlen = (xferp->cmd[7] << 8) + xferp->cmd[8];
		}

		size = retlen * d->logical_block_size;
		ofs *= d->logical_block_size;

		if (xferp->data_out_offset != size) {
			debug(", data_out == NULL, wanting %i bytes, \n\n",
			    (int)size);
			xferp->data_out_len = size;
			return 2;
		}

		debug(", data_out != NULL, OK :-)");

		debug("WRITE ofs=%i size=%i offset=%i\n", (int)ofs,
		    (int)size, (int)xferp->data_out_offset);

		diskimage__internal_access(d, 1, ofs,
		    xferp->data_out, size);

		/*  TODO: how about return code?  */

		/*  Is this really necessary?  */
		/*  fsync(fileno(d->f));  */

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_SYNCHRONIZE_CACHE:
		debug("SYNCHRONIZE_CACHE");

		if (xferp->cmd_len != 10)
			debug(" (weird len=%i)", xferp->cmd_len);

		/*  TODO: actualy care about cmd[]  */
		fsync(fileno(d->f));

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_START_STOP_UNIT:
		debug("START_STOP_UNIT");

		if (xferp->cmd_len != 6)
			debug(" (weird len=%i)", xferp->cmd_len);

		for (i=0; i<xferp->cmd_len; i++)
			debug(" %02x", xferp->cmd[i]);

		/*  TODO: actualy care about cmd[]  */

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_REQUEST_SENSE:
		debug("REQUEST_SENSE");

		retlen = xferp->cmd[4];

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if (xferp->cmd[1] != 0x00)
			fatal("WARNING: REQUEST_SENSE with cmd[1]=0x%02x not"
			    " yet implemented\n", (int)xferp->cmd[1]);

		if (retlen < 18) {
			fatal("WARNING: SCSI request sense len=%i, <18!\n",
			    (int)retlen);
			retlen = 18;
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in,
		    retlen, 1);

		xferp->data_in[0] = 0x80 + 0x70;/*  0x80 = valid,
						    0x70 = "current errors"  */
		xferp->data_in[2] = 0x00;	/*  SENSE KEY!  */

		if (d->filemark) {
			xferp->data_in[2] = 0x80;
		}
		debug(": [2]=0x%02x ", xferp->data_in[2]);

		printf(" XXX(!) \n");

		/*  TODO  */
		xferp->data_in[7] = retlen - 7;	/*  additional sense length  */
		/*  TODO  */

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_READ_BLOCK_LIMITS:
		debug("READ_BLOCK_LIMITS");

		retlen = 6;

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if (xferp->cmd[1] != 0x00)
			fatal("WARNING: READ_BLOCK_LIMITS with cmd[1]="
			    "0x%02x not yet implemented\n", (int)xferp->cmd[1]);

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len, &xferp->data_in,
		    retlen, 1);

		/*
		 *  data[0] is reserved, data[1..3] contain the maximum block
		 *  length limit, data[4..5] contain the minimum limit.
		 */

		{
			int max_limit = 32768;
			int min_limit = 128;

			xferp->data_in[1] = (max_limit >> 16) & 255;
			xferp->data_in[2] = (max_limit >>  8) & 255;
			xferp->data_in[3] =  max_limit        & 255;
			xferp->data_in[4] = (min_limit >>  8) & 255;
			xferp->data_in[5] =  min_limit        & 255;
		}

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_REWIND:
		debug("REWIND");

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if ((xferp->cmd[1] & 0xe0) != 0x00)
			fatal("WARNING: REWIND with cmd[1]=0x%02x not yet "
			    "implemented\n", (int)xferp->cmd[1]);

		/*  Close and reopen.  */

		if (d->f != NULL)
			fclose(d->f);

		d->f = fopen(d->fname, d->writable? "r+" : "r");
		if (d->f == NULL) {
			fprintf(stderr, "[ diskimage: could not (re)open "
			    "'%s' ]\n", d->fname);
			/*  TODO: return error  */
		}

		d->tape_offset = 0;
		d->tape_filenr = 0;
		d->filemark = 0;

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_SPACE:
		debug("SPACE");

		/*  TODO: bits 765 of buf[1] contains the LUN  */
		if ((xferp->cmd[1] & 0xe0) != 0x00)
			fatal("WARNING: SPACE with cmd[1]=0x%02x not yet "
			    "implemented\n", (int)xferp->cmd[1]);

		/*
		 *  Bits 2..0 of buf[1] contain the 'code' which describes how
		 *  we should space, and buf[2..4] contain the number of
		 *  operations.
		 */
		debug("[ SPACE: buf[] = %02x %02x %02x %02x %02x %02x ]\n",
		    xferp->cmd[0],
		    xferp->cmd[1],
		    xferp->cmd[2],
		    xferp->cmd[3],
		    xferp->cmd[4],
		    xferp->cmd[5]);

		switch (xferp->cmd[1] & 7) {
		case 1:	/*  Seek to a different file nr:  */
			{
				int diff = (xferp->cmd[2] << 16) +
				    (xferp->cmd[3] << 8) + xferp->cmd[4];

				/*  Negative seek offset:  */
				if (diff & (1 << 23))
					diff = - (16777216 - diff);

				d->tape_filenr += diff;
			}

			/*  At end of file, switch to the next tape file:  */
			if (d->filemark) {
				d->tape_filenr ++;
				d->filemark = 0;
			}

			debug("{ switching to tape file %i }", d->tape_filenr);
			diskimage__switch_tape(d);
			d->filemark = 0;
			break;
		default:
			fatal("[ diskimage.c: unimplemented SPACE type %i ]\n",
			    xferp->cmd[1] & 7);
		}

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICDROM_READ_SUBCHANNEL:
		/*
		 *  According to
		 *  http://mail-index.netbsd.org/port-i386/1997/03/03/0010.html:
		 *
		 *  "The READ_CD_CAPACITY, READ_SUBCHANNEL, and MODE_SELECT
		 *   commands have the same opcode in SCSI or ATAPI, but don't
		 *   have the same command structure"...
		 *
		 *  TODO: This still doesn't work. Hm.
		 */
		retlen = 48;

		debug("CDROM_READ_SUBCHANNEL/READ_CD_CAPACITY, cmd[1]=0x%02x",
		    xferp->cmd[1]);

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len,
		    &xferp->data_in, retlen, 1);

		diskimage_recalc_size(d);

		size = d->total_size / d->logical_block_size;
		if (d->total_size & (d->logical_block_size-1))
			size ++;

		xferp->data_in[0] = (size >> 24) & 255;
		xferp->data_in[1] = (size >> 16) & 255;
		xferp->data_in[2] = (size >> 8) & 255;
		xferp->data_in[3] = size & 255;

		xferp->data_in[4] = (d->logical_block_size >> 24) & 255;
		xferp->data_in[5] = (d->logical_block_size >> 16) & 255;
		xferp->data_in[6] = (d->logical_block_size >> 8) & 255;
		xferp->data_in[7] = d->logical_block_size & 255;

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICDROM_READ_TOC:
		debug("(CDROM_READ_TOC: ");
		debug("lun=%i msf=%i ",
		    xferp->cmd[1] >> 5, (xferp->cmd[1] >> 1) & 1);
		debug("starting_track=%i ", xferp->cmd[6]);
		retlen = xferp->cmd[7] * 256 + xferp->cmd[8];
		debug("allocation_len=%i)\n", retlen);

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len,
		    &xferp->data_in, retlen, 1);

		/*  TODO  */

		diskimage__return_default_status_and_message(xferp);
		break;

	case SCSICMD_MODE_SELECT:
		debug("[ SCSI MODE_SELECT: ");

		/*
		 *  TODO:
		 *
		 *  This is super-hardcoded for NetBSD's usage of mode_select
		 *  to set the size of CDROM sectors to 2048.
		 */

		if (xferp->data_out_offset == 0) {
			xferp->data_out_len = 12;	/*  TODO  */
			debug("data_out == NULL, wanting %i bytes ]\n",
			    (int)xferp->data_out_len);
			return 2;
		}

		debug("data_out!=NULL (OK), ");

		/*  TODO:  Care about cmd?  */

		/*  Set sector size to 2048:  */
		/*  00 05 00 08 00 03 ca 40 00 00 08 00  */
		if (xferp->data_out[0] == 0x00 &&
		    xferp->data_out[1] == 0x05 &&
		    xferp->data_out[2] == 0x00 &&
		    xferp->data_out[3] == 0x08) {
			d->logical_block_size =
			    (xferp->data_out[9] << 16) +
			    (xferp->data_out[10] << 8) +
			    xferp->data_out[11];
			debug("[ setting logical_block_size to %i ]\n",
			    d->logical_block_size);
		} else {
			int i;
			fatal("[ unknown MODE_SELECT: cmd =");
			for (i=0; i<xferp->cmd_len; i++)
				fatal(" %02x", xferp->cmd[i]);
			fatal(", data_out =");
			for (i=0; i<xferp->data_out_len; i++)
				fatal(" %02x", xferp->data_out[i]);
			fatal(" ]");
		}

		debug(" ]\n");
		diskimage__return_default_status_and_message(xferp);
		break;

	case 0x1e:
		debug("[ SCSI 0x%02x: TODO ]\n", xferp->cmd[0]);

		/*  TODO  */

		diskimage__return_default_status_and_message(xferp);
		break;

	case 0xbd:
		fatal("[ SCSI 0x%02x (len %i), TODO: ", xferp->cmd[0],
		    xferp->cmd_len);
		for (i=0; i<xferp->cmd_len; i++)
			fatal(" %02x", xferp->cmd[i]);
		fatal(" ]\n");

		/*
		 *  Used by Windows NT?
		 *
		 *  Not documented in http://www.danbbs.dk/~dino/
		 *		SCSI/SCSI2-D.html.
		 *  Google gave the answer "MECHANISM_STATUS" for ATAPI. Hm.
		 */

		if (xferp->cmd_len < 12) {
			fatal("WEIRD LEN?\n");
			retlen = 8;
		} else {
			retlen = xferp->cmd[8] * 256 + xferp->cmd[9];
		}

		/*  Return data:  */
		scsi_transfer_allocbuf(&xferp->data_in_len,
		    &xferp->data_in, retlen, 1);

		diskimage__return_default_status_and_message(xferp);

		break;

	default:
		fatal("[ UNIMPLEMENTED SCSI command 0x%02x, disk id=%i ]\n",
		    xferp->cmd[0], id);
		exit(1);
	}
	debug(" ]\n");

	return 1;
}


/*
 *  diskimage_access():
 *
 *  Read from or write to a disk image on a machine.
 *
 *  Returns 1 if the access completed successfully, 0 otherwise.
 */
int diskimage_access(struct machine *machine, int id, int type, int writeflag,
	off_t offset, unsigned char *buf, size_t len)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id)
			break;
		d = d->next;
	}

	if (d == NULL) {
		fatal("[ diskimage_access(): ERROR: trying to access a "
		    "non-existant %s disk image (id %i)\n",
		    diskimage_types[type], id);
		return 0;
	}

	return diskimage__internal_access(d, writeflag, offset, buf, len);
}


/*
 *  diskimage_add():
 *
 *  Add a disk image.  fname is the filename of the disk image.
 *  The filename may be prefixed with one or more modifiers, followed
 *  by a colon.
 *
 *	b	specifies that this is a bootable device
 *	c	CD-ROM (instead of a normal DISK)
 *	d	DISK (this is the default)
 *	f	FLOPPY (instead of SCSI)
 *	gH;S;	set geometry (H=heads, S=sectors per track, cylinders are
 *		automatically calculated). (This is ignored for floppies.)
 *	i	IDE (instead of SCSI)
 *	r       read-only (don't allow changes to the file)
 *	s	SCSI (this is the default)
 *	t	tape
 *	0-7	force a specific SCSI ID number
 *
 *  machine is assumed to be non-NULL.
 *  Returns an integer >= 0 identifying the disk image.
 */
int diskimage_add(struct machine *machine, char *fname)
{
	struct diskimage *d, *d2;
	int id = 0, override_heads=0, override_spt=0;
	int64_t bytespercyl;
	char *cp;
	int prefix_b=0, prefix_c=0, prefix_d=0, prefix_f=0, prefix_g=0;
	int prefix_i=0, prefix_r=0, prefix_s=0, prefix_t=0, prefix_id = -1;

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
			case 'f':
				prefix_f = 1;
				break;
			case 'g':
				prefix_g = 1;
				override_heads = atoi(fname);
				while (*fname != '\0' && *fname != ';')
					fname ++;
				if (*fname == ';')
					fname ++;
				override_spt = atoi(fname);
				while (*fname != '\0' && *fname != ';' &&
				    *fname != ':')
					fname ++;
				if (*fname == ';')
					fname ++;
				if (override_heads < 1 ||
				    override_spt < 1) {
					fatal("Bad geometry: heads=%i "
					    "spt=%i\n", override_heads,
					    override_spt);
					exit(1);
				}
				break;
			case 'i':
				prefix_i = 1;
				break;
			case 'r':
				prefix_r = 1;
				break;
			case 's':
				prefix_s = 1;
				break;
			case 't':
				prefix_t = 1;
				break;
			case ':':
				break;
			default:
				fprintf(stderr, "diskimage_add(): invalid "
				    "prefix char '%c'\n", c);
				exit(1);
			}
		}
	}

	/*  Allocate a new diskimage struct:  */
	d = malloc(sizeof(struct diskimage));
	if (d == NULL) {
		fprintf(stderr, "out of memory in diskimage_add()\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct diskimage));

	d2 = machine->first_diskimage;
	if (d2 == NULL) {
		machine->first_diskimage = d;
	} else {
		while (d2->next != NULL)
			d2 = d2->next;
		d2->next = d;
	}

	d->type = DISKIMAGE_SCSI;

	/*  Special cases: some machines usually have FLOPPY/IDE, not SCSI:  */
	if (machine->arch == ARCH_X86 ||
	    machine->machine_type == MACHINE_COBALT ||
	    machine->machine_type == MACHINE_EVBMIPS ||
	    machine->machine_type == MACHINE_HPCMIPS ||
	    machine->machine_type == MACHINE_CATS ||
	    machine->machine_type == MACHINE_NETWINDER ||
	    machine->machine_type == MACHINE_PS2)
		d->type = DISKIMAGE_IDE;

	if (prefix_i + prefix_f + prefix_s > 1) {
		fprintf(stderr, "Invalid disk image prefix(es). You can"
		    "only use one of i, f, and s\nfor each disk image.\n");
		exit(1);
	}

	if (prefix_i)
		d->type = DISKIMAGE_IDE;
	if (prefix_f)
		d->type = DISKIMAGE_FLOPPY;
	if (prefix_s)
		d->type = DISKIMAGE_SCSI;

	d->fname = strdup(fname);
	if (d->fname == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	d->logical_block_size = 512;

	/*
	 *  Is this a tape, CD-ROM or a normal disk?
	 *
	 *  An intelligent guess, if no prefixes are used, would be that
	 *  filenames ending with .iso or .cdr are CD-ROM images.
	 */
	if (prefix_t) {
		d->is_a_tape = 1;
	} else {
		if (prefix_c ||
		    ((strlen(d->fname) > 4 &&
		    (strcasecmp(d->fname + strlen(d->fname) - 4, ".cdr") == 0 ||
		    strcasecmp(d->fname + strlen(d->fname) - 4, ".iso") == 0))
		    && !prefix_d)
		   ) {
			d->is_a_cdrom = 1;

			/*
			 *  This is tricky. Should I use 512 or 2048 here?
			 *  NetBSD/pmax 1.6.2 and Ultrix likes 512 bytes
			 *  per sector, but NetBSD 2.0_BETA suddenly ignores
			 *  this value and uses 2048 instead.
			 *
			 *  OpenBSD/arc doesn't like 2048, it requires 512
			 *  to work correctly.
			 *
			 *  TODO
			 */

#if 0
			if (machine->machine_type == MACHINE_DEC)
				d->logical_block_size = 512;
			else
				d->logical_block_size = 2048;
#endif
			d->logical_block_size = 512;
		}
	}

	diskimage_recalc_size(d);

	if ((d->total_size == 720*1024 || d->total_size == 1474560
	    || d->total_size == 2949120 || d->total_size == 1228800)
	    && !prefix_i && !prefix_s)
		d->type = DISKIMAGE_FLOPPY;

	switch (d->type) {
	case DISKIMAGE_FLOPPY:
		if (d->total_size < 737280) {
			fatal("\nTODO: small (non-80-cylinder) floppies?\n\n");
			exit(1);
		}
		d->cylinders = 80;
		d->heads = 2;
		d->sectors_per_track = d->total_size / (d->cylinders *
		    d->heads * 512);
		break;
	default:/*  Non-floppies:  */
		d->heads = 16;
		d->sectors_per_track = 63;
		if (prefix_g) {
			d->chs_override = 1;
			d->heads = override_heads;
			d->sectors_per_track = override_spt;
		}
		bytespercyl = d->heads * d->sectors_per_track * 512;
		d->cylinders = d->total_size / bytespercyl;
		if (d->cylinders * bytespercyl < d->total_size)
			d->cylinders ++;
	}

	d->rpms = 3600;

	if (prefix_b)
		d->is_boot_device = 1;

	d->writable = access(fname, W_OK) == 0? 1 : 0;

	if (d->is_a_cdrom || prefix_r)
		d->writable = 0;

	d->f = fopen(fname, d->writable? "r+" : "r");
	if (d->f == NULL) {
		perror(fname);
		exit(1);
	}

	/*  Calculate which ID to use:  */
	if (prefix_id == -1) {
		int free = 0, collision = 1;

		while (collision) {
			collision = 0;
			d2 = machine->first_diskimage;
			while (d2 != NULL) {
				/*  (don't compare against ourselves :)  */
				if (d2 == d) {
					d2 = d2->next;
					continue;
				}
				if (d2->id == free && d2->type == d->type) {
					collision = 1;
					break;
				}
				d2 = d2->next;
			}
			if (!collision)
				id = free;
			else
				free ++;
		}
	} else {
		id = prefix_id;
		d2 = machine->first_diskimage;
		while (d2 != NULL) {
			/*  (don't compare against ourselves :)  */
			if (d2 == d) {
				d2 = d2->next;
				continue;
			}
			if (d2->id == id && d2->type == d->type) {
				fprintf(stderr, "disk image id %i "
				    "already in use\n", id);
				exit(1);
			}
			d2 = d2->next;
		}
	}

	d->id = id;

	return id;
}


/*
 *  diskimage_bootdev():
 *
 *  Returns the disk id of the device which we're booting from.  If typep is
 *  non-NULL, the type is returned as well.
 *
 *  If no disk was used as boot device, then -1 is returned. (In practice,
 *  this is used to fake network (tftp) boot.)
 */
int diskimage_bootdev(struct machine *machine, int *typep)
{
	struct diskimage *d;

	d = machine->first_diskimage;
	while (d != NULL) {
		if (d->is_boot_device) {
			if (typep != NULL)
				*typep = d->type;
			return d->id;
		}
		d = d->next;
	}

	d = machine->first_diskimage;
	if (d != NULL) {
		if (typep != NULL)
			*typep = d->type;
		return d->id;
	}

	return -1;
}


/*
 *  diskimage_is_a_cdrom():
 *
 *  Returns 1 if a disk image is a CDROM, 0 otherwise.
 */
int diskimage_is_a_cdrom(struct machine *machine, int id, int type)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id)
			return d->is_a_cdrom;
		d = d->next;
	}
	return 0;
}


/*
 *  diskimage_is_a_tape():
 *
 *  Returns 1 if a disk image is a tape, 0 otherwise.
 *
 *  (Used in src/machine.c, to select 'rz' vs 'tz' for DECstation
 *  boot strings.)
 */
int diskimage_is_a_tape(struct machine *machine, int id, int type)
{
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		if (d->type == type && d->id == id)
			return d->is_a_tape;
		d = d->next;
	}
	return 0;
}


/*
 *  diskimage_dump_info():
 *
 *  Debug dump of all diskimages that are loaded for a specific machine.
 */
void diskimage_dump_info(struct machine *machine)
{
	int iadd=4;
	struct diskimage *d = machine->first_diskimage;

	while (d != NULL) {
		debug("diskimage: %s\n", d->fname);
		debug_indentation(iadd);

		switch (d->type) {
		case DISKIMAGE_SCSI:
			debug("SCSI");
			break;
		case DISKIMAGE_IDE:
			debug("IDE");
			break;
		case DISKIMAGE_FLOPPY:
			debug("FLOPPY");
			break;
		default:
			debug("UNKNOWN type %i", d->type);
		}

		debug(" %s", d->is_a_tape? "TAPE" :
			(d->is_a_cdrom? "CD-ROM" : "DISK"));
		debug(" id %i, ", d->id);
		debug("%s, ", d->writable? "read/write" : "read-only");

		if (d->type == DISKIMAGE_FLOPPY)
			debug("%lli KB", (long long) (d->total_size / 1024));
		else
			debug("%lli MB", (long long) (d->total_size / 1048576));

		if (d->type == DISKIMAGE_FLOPPY || d->chs_override)
			debug(" (CHS=%i,%i,%i)", d->cylinders, d->heads,
			    d->sectors_per_track);
		else
			debug(" (%lli sectors)", (long long)
			   (d->total_size / 512));

		if (d->is_boot_device)
			debug(" (BOOT)");
		debug("\n");

		debug_indentation(-iadd);

		d = d->next;
	}
}


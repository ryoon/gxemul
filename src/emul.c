/*
 *  Copyright (C) 2003-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.c,v 1.256 2006-07-01 21:15:45 debug Exp $
 *
 *  Emulation startup and misc. routines.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "arcbios.h"
#include "cpu.h"
#include "emul.h"
#include "console.h"
#include "debugger.h"
#include "device.h"
#include "diskimage.h"
#include "exec_elf.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu_types.h"
#include "misc.h"
#include "net.h"
#include "sgi_arcbios.h"
#include "x11.h"


extern int extra_argc;
extern char **extra_argv;

extern int verbose;
extern int quiet_mode;
extern int force_debugger_at_exit;
extern int single_step;
extern int old_show_trace_tree;
extern int old_instruction_trace;
extern int old_quiet_mode;
extern int quiet_mode;

extern struct emul *debugger_emul;
extern struct diskimage *diskimages[];

static char *diskimage_types[] = DISKIMAGE_TYPES;


static void print_separator(void)
{
	int i = 79;
	while (i-- > 0)
		debug("-");
	debug("\n");
}


/*
 *  add_dump_points():
 *
 *  Take the strings breakpoint_string[] and convert to addresses
 *  (and store them in breakpoint_addr[]).
 *
 *  TODO: This function should be moved elsewhere.
 */
static void add_dump_points(struct machine *m)
{
	int i;
	int string_flag;
	uint64_t dp;

	for (i=0; i<m->n_breakpoints; i++) {
		string_flag = 0;
		dp = strtoull(m->breakpoint_string[i], NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&m->symbol_context,
			    m->breakpoint_string[i], &addr);
			if (!res) {
				fprintf(stderr,
				    "ERROR! Breakpoint '%s' could not be"
					" parsed\n",
				    m->breakpoint_string[i]);
			} else {
				dp = addr;
				string_flag = 1;
			}
		}

		/*
		 *  TODO:  It would be nice if things like   symbolname+0x1234
		 *  were automatically converted into the correct address.
		 */

		if (m->arch == ARCH_MIPS) {
			if ((dp >> 32) == 0 && ((dp >> 31) & 1))
				dp |= 0xffffffff00000000ULL;
		}

		m->breakpoint_addr[i] = dp;

		debug("breakpoint %i: 0x%llx", i, (long long)dp);
		if (string_flag)
			debug(" (%s)", m->breakpoint_string[i]);
		debug("\n");
	}
}


/*
 *  fix_console():
 */
static void fix_console(void)
{
	console_deinit();
}


/*
 *  iso_load_bootblock():
 *
 *  Try to load a kernel from an ISO 9660 disk image. iso_type is 1 for
 *  "CD001" (standard), 2 for "CDW01" (ECMA), and 3 for "CDROM" (Sierra).
 *
 *  TODO: This function uses too many magic offsets and so on; it should be
 *  cleaned up some day.
 *
 *  Returns 1 on success, 0 on failure.
 */
static int iso_load_bootblock(struct machine *m, struct cpu *cpu,
	int disk_id, int disk_type, int iso_type, unsigned char *buf,
	int *n_loadp, char ***load_namesp)
{
	char str[35];
	int filenr, i, ofs, dirlen, res = 0, res2, iadd = DEBUG_INDENTATION;
	int found_dir;
	uint64_t dirofs;
	uint64_t fileofs, filelen;
	unsigned char *dirbuf = NULL, *dp;
	unsigned char *match_entry = NULL;
	char *p, *filename_orig;
	char *filename = strdup(cpu->machine->boot_kernel_filename);
	unsigned char *filebuf = NULL;
	char *tmpfname = NULL;
	char **new_array;
	int tmpfile_handle;

	if (filename == NULL) {
		fatal("out of memory\n");
		exit(1);
	}
	filename_orig = filename;

	debug("ISO9660 boot:\n");
	debug_indentation(iadd);

	/*  Volume ID:  */
	ofs = iso_type == 3? 48 : 40;
	memcpy(str, buf + ofs, sizeof(str));
	str[32] = '\0';  i = 31;
	while (i >= 0 && str[i]==' ')
		str[i--] = '\0';
	if (str[0])
		debug("\"%s\"", str);
	else {
		/*  System ID:  */
		ofs = iso_type == 3? 16 : 8;
		memcpy(str, buf + ofs, sizeof(str));
		str[32] = '\0';  i = 31;
		while (i >= 0 && str[i]==' ')
			str[i--] = '\0';
		if (str[0])
			debug("\"%s\"", str);
		else
			debug("(no ID)");
	}

	debug(":%s\n", filename);


	/*
	 *  Traverse the directory structure to find the kernel.
	 */

	dirlen = buf[0x84] + 256*buf[0x85] + 65536*buf[0x86];
	if (dirlen != buf[0x8b] + 256*buf[0x8a] + 65536*buf[0x89])
		fatal("WARNING: Root directory length mismatch?\n");

	dirofs = (int64_t)(buf[0x8c] + (buf[0x8d] << 8) + (buf[0x8e] << 16) +
	    ((uint64_t)buf[0x8f] << 24)) * 2048;

	/*  debug("root = %i bytes at 0x%llx\n", dirlen, (long long)dirofs);  */

	dirbuf = malloc(dirlen);
	if (dirbuf == NULL) {
		fatal("out of memory in iso_load_bootblock()\n");
		exit(1);
	}

	res2 = diskimage_access(m, disk_id, disk_type, 0, dirofs, dirbuf,
	    dirlen);
	if (!res2) {
		fatal("Couldn't read the disk image. Aborting.\n");
		goto ret;
	}

	found_dir = 1;	/*  Assume root dir  */
	dp = dirbuf; filenr = 1;
	p = NULL;
	while (dp < dirbuf + dirlen) {
		size_t i, nlen = dp[0];
		int x = dp[2] + (dp[3] << 8) + (dp[4] << 16) +
		    ((uint64_t)dp[5] << 24);
		int y = dp[6] + (dp[7] << 8);
		char direntry[65];

		dp += 8;

		/*
		 *  As long as there is an \ or / in the filename, then we
		 *  have not yet found the directory.
		 */
		p = strchr(filename, '/');
		if (p == NULL)
			p = strchr(filename, '\\');

		/*  debug("%i%s: %i, %i, \"", filenr, filenr == found_dir?
		    " [CURRENT]" : "", x, y);  */
		for (i=0; i<nlen && i<sizeof(direntry)-1; i++)
			if (dp[i]) {
				direntry[i] = dp[i];
				/*  debug("%c", dp[i]);  */
			} else
				break;
		/*  debug("\"\n");  */
		direntry[i] = '\0';

		/*  A directory name match?  */
		if (p != NULL && strncasecmp(filename, direntry, nlen) == 0
		    && nlen == (size_t)p - (size_t)filename && found_dir == y) {
			found_dir = filenr;
			filename = p+1;
			dirofs = 2048 * (int64_t)x;
		}

		dp += nlen;

		/*  16-bit aligned lenght:  */
		if (nlen & 1)
			dp ++;

		filenr ++;
	}

	p = strchr(filename, '/');
	if (p == NULL)
		p = strchr(filename, '\\');

	if (p != NULL) {
		char *blah = filename_orig;

		fatal("could not find '%s' in /", filename);

		/*  Print the first part of the filename:  */
		while (blah != filename)
			fatal("%c", *blah++);
		
		fatal("\n");
		goto ret;
	}

	/*  debug("dirofs = 0x%llx\n", (long long)dirofs);  */

	/*  Free the old dirbuf, and allocate a new one:  */
	free(dirbuf);
	dirbuf = malloc(512);
	if (dirbuf == NULL) {
		fatal("out of memory in iso_load_bootblock()\n");
		exit(1);
	}

	for (;;) {
		size_t len, i;

		/*  Too close to another sector? Then realign.  */
		if ((dirofs & 2047) + 70 > 2047) {
			dirofs = (dirofs | 2047) + 1;
			/*  debug("realign dirofs = 0x%llx\n", dirofs);  */
		}

		res2 = diskimage_access(m, disk_id, disk_type, 0, dirofs,
		    dirbuf, 256);
		if (!res2) {
			fatal("Couldn't read the disk image. Aborting.\n");
			goto ret;
		}

		dp = dirbuf;
		len = dp[0];
		if (len < 2)
			break;

		/*
		 *  TODO: Actually parse the directory entry!
		 *
		 *  Haha, this must be rewritten.
		 */
		for (i=32; i<len; i++) {
			if (i < len - strlen(filename))
				if (strncasecmp(filename, (char *)dp + i,
				    strlen(filename)) == 0) {
					/*  The filename was found somewhere
					    in the directory entry.  */
					if (match_entry != NULL) {
						fatal("TODO: I'm too lazy to"
						    " implement a correct "
						    "directory parser right "
						    "now... (BUG)\n");
						exit(1);
					}
					match_entry = malloc(512);
					if (match_entry == NULL) {
						fatal("out of memory\n");
						exit(1);
					}
					memcpy(match_entry, dp, 512);
					break;
				}
		}

		dirofs += len;
	}

	if (match_entry == NULL) {
		char *blah = filename_orig;

		fatal("could not find '%s' in /", filename);

		/*  Print the first part of the filename:  */
		while (blah != filename)
			fatal("%c", *blah++);
		
		fatal("\n");
		goto ret;
	}

	fileofs = match_entry[2] + (match_entry[3] << 8) +
	    (match_entry[4] << 16) + ((uint64_t)match_entry[5] << 24);
	filelen = match_entry[10] + (match_entry[11] << 8) +
	    (match_entry[12] << 16) + ((uint64_t)match_entry[13] << 24);
	fileofs *= 2048;

	/*  debug("filelen=%llx fileofs=%llx\n", (long long)filelen,
	    (long long)fileofs);  */

	filebuf = malloc(filelen);
	if (filebuf == NULL) {
		fatal("could not allocate %lli bytes to read the file"
		    " from the disk image!\n", (long long)filelen);
		goto ret;
	}

	tmpfname = strdup("/tmp/gxemul.XXXXXXXXXXXX");

	res2 = diskimage_access(m, disk_id, disk_type, 0, fileofs, filebuf,
	    filelen);
	if (!res2) {
		fatal("could not read the file from the disk image!\n");
		goto ret;
	}

	tmpfile_handle = mkstemp(tmpfname);
	if (tmpfile_handle < 0) {
		fatal("could not create %s\n", tmpfname);
		exit(1);
	}
	write(tmpfile_handle, filebuf, filelen);
	close(tmpfile_handle);

	debug("extracted %lli bytes into %s\n", (long long)filelen, tmpfname);

	/*  Add the temporary filename to the load_namesp array:  */
	(*n_loadp)++;
	new_array = malloc(sizeof(char *) * (*n_loadp));
	if (new_array == NULL) {
		fatal("out of memory\n");
		exit(1);
	}
	memcpy(new_array, *load_namesp, sizeof(char *) * (*n_loadp));
	*load_namesp = new_array;

	/*  This adds a Backspace char in front of the filename; this
	    is a special hack which causes the file to be removed once
	    it has been loaded.  */
	tmpfname = realloc(tmpfname, strlen(tmpfname) + 2);
	memmove(tmpfname + 1, tmpfname, strlen(tmpfname) + 1);
	tmpfname[0] = 8;

	(*load_namesp)[*n_loadp - 1] = tmpfname;

	res = 1;

ret:
	if (dirbuf != NULL)
		free(dirbuf);

	if (filebuf != NULL)
		free(filebuf);

	if (match_entry != NULL)
		free(match_entry);

	free(filename_orig);

	debug_indentation(-iadd);
	return res;
}


/*
 *  apple_load_bootblock():
 *
 *  Try to load a kernel from a disk image with an Apple Partition Table.
 *
 *  TODO: This function uses too many magic offsets and so on; it should be
 *  cleaned up some day. See http://www.awprofessional.com/articles/
 *	article.asp?p=376123&seqNum=3&rl=1  for some info on the Apple
 *  partition format.
 *
 *  Returns 1 on success, 0 on failure.
 */
static int apple_load_bootblock(struct machine *m, struct cpu *cpu,
	int disk_id, int disk_type, int *n_loadp, char ***load_namesp)
{
	unsigned char buf[0x8000];
	int res, partnr, n_partitions = 0, n_hfs_partitions = 0;
	uint64_t hfs_start, hfs_length;

	res = diskimage_access(m, disk_id, disk_type, 0, 0x0, buf, sizeof(buf));
	if (!res) {
		fatal("apple_load_bootblock: couldn't read the disk "
		    "image. Aborting.\n");
		return 0;
	}

	partnr = 0;
	do {
		int start, length;
		int ofs = 0x200 * (partnr + 1);
		if (partnr == 0)
			n_partitions = buf[ofs + 7];
		start = ((uint64_t)buf[ofs + 8] << 24) + (buf[ofs + 9] << 16) +
		    (buf[ofs + 10] << 8) + buf[ofs + 11];
		length = ((uint64_t)buf[ofs+12] << 24) + (buf[ofs + 13] << 16) +
		    (buf[ofs + 14] << 8) + buf[ofs + 15];

		debug("partition %i: '%s', type '%s', start %i, length %i\n",
		    partnr, buf + ofs + 0x10, buf + ofs + 0x30,
		    start, length);

		if (strcmp((char *)buf + ofs + 0x30, "Apple_HFS") == 0) {
			n_hfs_partitions ++;
			hfs_start = 512 * start;
			hfs_length = 512 * length;
		}

		/*  Any more partitions?  */
		partnr ++;
	} while (partnr < n_partitions);

	if (n_hfs_partitions == 0) {
		fatal("Error: No HFS partition found! TODO\n");
		return 0;
	}
	if (n_hfs_partitions >= 2) {
		fatal("Error: Too many HFS partitions found! TODO\n");
		return 0;
	}

	return 0;
}


/*
 *  load_bootblock():
 *
 *  For some emulation modes, it is possible to boot from a harddisk image by
 *  loading a bootblock from a specific disk offset into memory, and executing
 *  that, instead of requiring a separate kernel file.  It is then up to the
 *  bootblock to load a kernel.
 *
 *  Returns 1 on success, 0 on failure.
 */
static int load_bootblock(struct machine *m, struct cpu *cpu,
	int *n_loadp, char ***load_namesp)
{
	int boot_disk_id, boot_disk_type = 0, n_blocks, res, readofs,
	    iso_type, retval = 0;
	unsigned char minibuf[0x20];
	unsigned char *bootblock_buf;
	uint64_t bootblock_offset;
	uint64_t bootblock_loadaddr, bootblock_pc;

	boot_disk_id = diskimage_bootdev(m, &boot_disk_type);
	if (boot_disk_id < 0)
		return 0;

	switch (m->machine_type) {
	case MACHINE_PMAX:
		/*
		 *  The first few bytes of a disk contains information about
		 *  where the bootblock(s) are located. (These are all 32-bit
		 *  little-endian words.)
		 *
		 *  Offset 0x10 = load address
		 *         0x14 = initial PC value
		 *         0x18 = nr of 512-byte blocks to read
		 *         0x1c = offset on disk to where the bootblocks
		 *                are (in 512-byte units)
		 *         0x20 = nr of blocks to read...
		 *         0x24 = offset...
		 *
		 *  nr of blocks to read and offset are repeated until nr of
		 *  blocks to read is zero.
		 */
		res = diskimage_access(m, boot_disk_id, boot_disk_type, 0, 0,
		    minibuf, sizeof(minibuf));

		bootblock_loadaddr = minibuf[0x10] + (minibuf[0x11] << 8)
		  + (minibuf[0x12] << 16) + ((uint64_t)minibuf[0x13] << 24);

		/*  Convert loadaddr to uncached:  */
		if ((bootblock_loadaddr & 0xf0000000ULL) != 0x80000000 &&
		    (bootblock_loadaddr & 0xf0000000ULL) != 0xa0000000)
			fatal("\nWARNING! Weird load address 0x%08x.\n\n",
			    (int)bootblock_loadaddr);
		bootblock_loadaddr &= 0x0fffffffULL;
		bootblock_loadaddr |= 0xffffffffa0000000ULL;

		bootblock_pc = minibuf[0x14] + (minibuf[0x15] << 8)
		  + (minibuf[0x16] << 16) + ((uint64_t)minibuf[0x17] << 24);

		bootblock_pc &= 0x0fffffffULL;
		bootblock_pc |= 0xffffffffa0000000ULL;
		cpu->pc = bootblock_pc;

		debug("DEC boot: loadaddr=0x%08x, pc=0x%08x",
		    (int)bootblock_loadaddr, (int)bootblock_pc);

		readofs = 0x18;

		for (;;) {
			res = diskimage_access(m, boot_disk_id, boot_disk_type,
			    0, readofs, minibuf, sizeof(minibuf));
			if (!res) {
				fatal("Couldn't read the disk image. "
				    "Aborting.\n");
				return 0;
			}

			n_blocks = minibuf[0] + (minibuf[1] << 8)
			  + (minibuf[2] << 16) + ((uint64_t)minibuf[3] << 24);

			bootblock_offset = (minibuf[4] + (minibuf[5] << 8) +
			  (minibuf[6]<<16) + ((uint64_t)minibuf[7]<<24)) * 512;

			if (n_blocks < 1)
				break;

			debug(readofs == 0x18? ": %i" : " + %i", n_blocks);

			if (n_blocks * 512 > 65536)
				fatal("\nWARNING! Unusually large bootblock "
				    "(%i bytes)\n\n", n_blocks * 512);

			bootblock_buf = malloc(n_blocks * 512);
			if (bootblock_buf == NULL) {
				fprintf(stderr, "out of memory in "
				    "load_bootblock()\n");
				exit(1);
			}

			res = diskimage_access(m, boot_disk_id, boot_disk_type,
			    0, bootblock_offset, bootblock_buf, n_blocks * 512);
			if (!res) {
				fatal("WARNING: could not load bootblocks from"
				    " disk offset 0x%llx\n",
				    (long long)bootblock_offset);
			}

			store_buf(cpu, bootblock_loadaddr,
			    (char *)bootblock_buf, n_blocks * 512);

			bootblock_loadaddr += 512*n_blocks;
			free(bootblock_buf);
			readofs += 8;
		}

		debug(readofs == 0x18? ": no blocks?\n" : " blocks\n");
		return 1;

	case MACHINE_X86:
		/*  TODO: "El Torito" etc?  */
		if (diskimage_is_a_cdrom(cpu->machine, boot_disk_id,
		    boot_disk_type))
			break;

		bootblock_buf = malloc(512);
		if (bootblock_buf == NULL) {
			fprintf(stderr, "Out of memory.\n");
			exit(1);
		}

		debug("loading PC bootsector from %s id %i\n",
		    diskimage_types[boot_disk_type], boot_disk_id);

		res = diskimage_access(m, boot_disk_id, boot_disk_type, 0, 0,
		    bootblock_buf, 512);
		if (!res) {
			fatal("Couldn't read the disk image. Aborting.\n");
			return 0;
		}

		if (bootblock_buf[510] != 0x55 || bootblock_buf[511] != 0xaa)
			debug("WARNING! The 0x55,0xAA marker is missing! "
			    "Booting anyway.\n");
		store_buf(cpu, 0x7c00, (char *)bootblock_buf, 512);
		free(bootblock_buf);

		return 1;
	}


	/*
	 *  Try reading a kernel manually from the disk. The code here
	 *  does not rely on machine-dependent boot blocks etc.
	 */
	/*  ISO9660: (0x800 bytes at 0x8000)  */
	bootblock_buf = malloc(0x800);
	if (bootblock_buf == NULL) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	res = diskimage_access(m, boot_disk_id, boot_disk_type,
	    0, 0x8000, bootblock_buf, 0x800);
	if (!res) {
		fatal("Couldn't read the disk image. Aborting.\n");
		return 0;
	}

	iso_type = 0;
	if (strncmp((char *)bootblock_buf+1, "CD001", 5) == 0)
		iso_type = 1;
	if (strncmp((char *)bootblock_buf+1, "CDW01", 5) == 0)
		iso_type = 2;
	if (strncmp((char *)bootblock_buf+1, "CDROM", 5) == 0)
		iso_type = 3;

	if (iso_type != 0) {
		/*  We can't load a kernel if the name
		    isn't specified.  */
		if (cpu->machine->boot_kernel_filename == NULL ||
		    cpu->machine->boot_kernel_filename[0] == '\0')
			fatal("\nISO9660 filesystem, but no kernel "
			    "specified? (Use the -j option.)\n");
		else
			retval = iso_load_bootblock(m, cpu, boot_disk_id,
			    boot_disk_type, iso_type, bootblock_buf,
			    n_loadp, load_namesp);
	}

	if (retval != 0)
		goto ret_ok;

	/*  Apple parition table:  */
	res = diskimage_access(m, boot_disk_id, boot_disk_type,
	    0, 0x0, bootblock_buf, 0x800);
	if (!res) {
		fatal("Couldn't read the disk image. Aborting.\n");
		return 0;
	}
	if (bootblock_buf[0x000] == 'E' && bootblock_buf[0x001] == 'R' &&
	    bootblock_buf[0x200] == 'P' && bootblock_buf[0x201] == 'M') {
		/*  We can't load a kernel if the name
		    isn't specified.  */
		if (cpu->machine->boot_kernel_filename == NULL ||
		    cpu->machine->boot_kernel_filename[0] == '\0')
			fatal("\nApple partition table, but no kernel "
			    "specified? (Use the -j option.)\n");
		else
			retval = apple_load_bootblock(m, cpu, boot_disk_id,
			    boot_disk_type, n_loadp, load_namesp);
	}

ret_ok:
	free(bootblock_buf);
	return retval;
}


/*
 *  emul_new():
 *
 *  Returns a reasonably initialized struct emul.
 */
struct emul *emul_new(char *name)
{
	struct emul *e;
	e = malloc(sizeof(struct emul));
	if (e == NULL) {
		fprintf(stderr, "out of memory in emul_new()\n");
		exit(1);
	}

	memset(e, 0, sizeof(struct emul));

	/*  Sane default values:  */
	e->n_machines = 0;
	e->next_serial_nr = 1;

	if (name != NULL) {
		e->name = strdup(name);
		if (e->name == NULL) {
			fprintf(stderr, "out of memory in emul_new()\n");
			exit(1);
		}
	}

	return e;
}


/*
 *  emul_add_machine():
 *
 *  Calls machine_new(), adds the new machine into the emul struct, and
 *  returns a pointer to the new machine.
 *
 *  This function should be used instead of manually calling machine_new().
 */
struct machine *emul_add_machine(struct emul *e, char *name)
{
	struct machine *m;

	m = machine_new(name, e);
	m->serial_nr = (e->next_serial_nr ++);

	e->n_machines ++;
	e->machines = realloc(e->machines,
	    sizeof(struct machine *) * e->n_machines);
	if (e->machines == NULL) {
		fprintf(stderr, "emul_add_machine(): out of memory\n");
		exit(1);
	}

	e->machines[e->n_machines - 1] = m;
	return m;
}


/*
 *  add_arc_components():
 *
 *  This function adds ARCBIOS memory descriptors for the loaded program,
 *  and ARCBIOS components for SCSI devices.
 */
static void add_arc_components(struct machine *m)
{
	struct cpu *cpu = m->cpus[m->bootstrap_cpu];
	uint64_t start = cpu->pc & 0x1fffffff;
	uint64_t len = 0xc00000 - start;
	struct diskimage *d;
	uint64_t scsicontroller, scsidevice, scsidisk;

	if ((cpu->pc >> 60) != 0xf) {
		start = cpu->pc & 0xffffffffffULL;
		len = 0xc00000 - start;
	}

	len += 1048576 * m->memory_offset_in_mb;

	/*
	 *  NOTE/TODO: magic 12MB end of load program area
	 *
	 *  Hm. This breaks the old FreeBSD/MIPS snapshots...
	 */
#if 0
	arcbios_add_memory_descriptor(cpu,
	    0x60000 + m->memory_offset_in_mb * 1048576,
	    start-0x60000 - m->memory_offset_in_mb * 1048576,
	    ARCBIOS_MEM_FreeMemory);
#endif
	arcbios_add_memory_descriptor(cpu,
	    start, len, ARCBIOS_MEM_LoadedProgram);

	scsicontroller = arcbios_get_scsicontroller(m);
	if (scsicontroller == 0)
		return;

	/*  TODO: The device 'name' should defined be somewhere else.  */

	d = m->first_diskimage;
	while (d != NULL) {
		if (d->type == DISKIMAGE_SCSI) {
			int a, b, flags = COMPONENT_FLAG_Input;
			char component_string[100];
			char *name = "DEC     RZ58     (C) DEC2000";

			/*  Read-write, or read-only?  */
			if (d->writable)
				flags |= COMPONENT_FLAG_Output;
			else
				flags |= COMPONENT_FLAG_ReadOnly;

			a = COMPONENT_TYPE_DiskController;
			b = COMPONENT_TYPE_DiskPeripheral;

			if (d->is_a_cdrom) {
				flags |= COMPONENT_FLAG_Removable;
				a = COMPONENT_TYPE_CDROMController;
				b = COMPONENT_TYPE_FloppyDiskPeripheral;
				name = "NEC     CD-ROM CDR-210P 1.0 ";
			}

			scsidevice = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_ControllerClass,
			    a, flags, 1, 2, d->id, 0xffffffff,
			    name, scsicontroller, NULL, 0);

			scsidisk = arcbios_addchild_manual(cpu,
			    COMPONENT_CLASS_PeripheralClass,
			    b, flags, 1, 2, 0, 0xffffffff, NULL,
			    scsidevice, NULL, 0);

			/*
			 *  Add device string to component address mappings:
			 *  "scsi(0)disk(0)rdisk(0)partition(0)"
			 */

			if (d->is_a_cdrom) {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)cdrom(%i)fdisk(0)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidisk);
			} else {
				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidevice);

				snprintf(component_string,
				    sizeof(component_string),
				    "scsi(0)disk(%i)rdisk(0)", d->id);
				arcbios_add_string_to_component(m,
				    component_string, scsidisk);
			}
		}

		d = d->next;
	}
}


/*
 *  emul_machine_setup():
 *
 *	o)  Initialize the hardware (RAM, devices, CPUs, ...) which
 *	    will be emulated in this machine.
 *
 *	o)  Load ROM code and/or other programs into emulated memory.
 *
 *	o)  Special hacks needed after programs have been loaded.
 */
void emul_machine_setup(struct machine *m, int n_load, char **load_names,
	int n_devices, char **device_names)
{
	struct cpu *cpu;
	int i, iadd = DEBUG_INDENTATION;
	uint64_t memory_amount, entrypoint = 0, gp = 0, toc = 0;
	int byte_order;

	debug("machine \"%s\":\n", m->name);
	debug_indentation(iadd);

	/*  For userland-only, this decides which ARCH/cpu_name to use:  */
	if (m->machine_type == MACHINE_USERLAND && m->userland_emul != NULL) {
		useremul_name_to_useremul(NULL, m->userland_emul,
		    &m->arch, &m->machine_name, &m->cpu_name);
		if (m->arch == ARCH_NOARCH) {
			printf("Unsupported userland emulation mode.\n");
			exit(1);
		}
	}

	if (m->machine_type == MACHINE_NONE) {
		fatal("No machine type specified?\n");
		exit(1);
	}

	m->cpu_family = cpu_family_ptr_by_number(m->arch);

	if (m->arch == ARCH_ALPHA)
		m->arch_pagesize = 8192;

	machine_memsize_fix(m);

	/*
	 *  Create the system's memory:
	 *
	 *  (Don't print the amount for userland-only emulation; the
	 *  size doesn't matter.)
	 */
	if (m->machine_type != MACHINE_USERLAND)
		debug("memory: %i MB", m->physical_ram_in_mb);
	memory_amount = (uint64_t)m->physical_ram_in_mb * 1048576;
	if (m->memory_offset_in_mb > 0) {
		/*
		 *  A special hack is used for some SGI models,
		 *  where memory is offset by 128MB to leave room for
		 *  EISA space and other things.
		 */
		debug(" (offset by %iMB)", m->memory_offset_in_mb);
		memory_amount += 1048576 * m->memory_offset_in_mb;
	}
	m->memory = memory_new(memory_amount, m->arch);
	if (m->machine_type != MACHINE_USERLAND)
		debug("\n");

	/*  Create CPUs:  */
	if (m->cpu_name == NULL)
		machine_default_cputype(m);
	if (m->ncpus == 0) {
		/*  TODO: This should be moved elsewhere...  */
		if (m->machine_type == MACHINE_BEBOX)
			m->ncpus = 2;
		else if (m->machine_type == MACHINE_ARC &&
		    m->machine_subtype == MACHINE_ARC_NEC_R96)
			m->ncpus = 2;
		else if (m->machine_type == MACHINE_ARC &&
		    m->machine_subtype == MACHINE_ARC_NEC_R98)
			m->ncpus = 4;
		else
			m->ncpus = 1;
	}
	m->cpus = malloc(sizeof(struct cpu *) * m->ncpus);
	if (m->cpus == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(m->cpus, 0, sizeof(struct cpu *) * m->ncpus);

	debug("cpu0");
	if (m->ncpus > 1)
		debug(" .. cpu%i", m->ncpus - 1);
	debug(": ");
	for (i=0; i<m->ncpus; i++) {
		m->cpus[i] = cpu_new(m->memory, m, i, m->cpu_name);
		if (m->cpus[i] == NULL) {
			fprintf(stderr, "Unable to create CPU object. "
			    "Aborting.");
			exit(1);
		}
	}
	debug("\n");

#if 0
	/*  Special case: The Playstation Portable has an additional CPU:  */
	if (m->machine_type == MACHINE_PSP) {
		debug("cpu%i: ", m->ncpus);
		m->cpus[m->ncpus] = cpu_new(m->memory, m,
		    0  /*  use 0 here to show info with debug()  */,
		    "Allegrex" /*  TODO  */);
		debug("\n");
		m->ncpus ++;
	}
#endif

	if (m->use_random_bootstrap_cpu)
		m->bootstrap_cpu = random() % m->ncpus;
	else
		m->bootstrap_cpu = 0;

	cpu = m->cpus[m->bootstrap_cpu];

	/*  Set cpu->useremul_syscall, and use userland_memory_rw:  */
	if (m->userland_emul != NULL) {
		useremul_name_to_useremul(cpu,
		    m->userland_emul, NULL, NULL, NULL);

		switch (m->arch) {
#ifdef ENABLE_ALPHA
		case ARCH_ALPHA:
			cpu->memory_rw = alpha_userland_memory_rw;
			break;
#endif
		default:cpu->memory_rw = userland_memory_rw;
		}
	}

	if (m->use_x11)
		x11_init(m);

	/*  Fill memory with random bytes:  */
	if (m->random_mem_contents) {
		for (i=0; i<m->physical_ram_in_mb * 1048576; i+=256) {
			unsigned char data[256];
			unsigned int j;
			for (j=0; j<sizeof(data); j++)
				data[j] = random() & 255;
			cpu->memory_rw(cpu, m->memory, i, data, sizeof(data),
			    MEM_WRITE, CACHE_NONE | NO_EXCEPTIONS | PHYSICAL);
		}
	}

	if (m->userland_emul != NULL) {
		/*
		 *  For userland-only emulation, no machine emulation
		 *  is needed.
		 */
	} else {
		for (i=0; i<n_devices; i++)
			device_add(m, device_names[i]);

		machine_setup(m);
	}

	diskimage_dump_info(m);
	console_debug_dump(m);

	/*  Load files (ROM code, boot code, ...) into memory:  */
	if (n_load == 0) {
		if (m->first_diskimage != NULL) {
			if (!load_bootblock(m, cpu, &n_load, &load_names)) {
				fprintf(stderr, "\nNo executable files were"
				    " specified, and booting directly from disk"
				    " failed.\n");
				exit(1);
			}
		} else {
			fprintf(stderr, "No executable file(s) loaded, and "
			    "we are not booting directly from a disk image."
			    "\nAborting.\n");
			exit(1);
		}
	}

	while (n_load > 0) {
		FILE *tmp_f;
		char *name_to_load = *load_names;
		int remove_after_load = 0;

		/*  Special hack for removing temporary files:  */
		if (name_to_load[0] == 8) {
			name_to_load ++;
			remove_after_load = 1;
		}

		/*
		 *  gzipped files are automagically gunzipped:
		 *  NOTE/TODO: This isn't secure. system() is used.
		 */
		tmp_f = fopen(name_to_load, "r");
		if (tmp_f != NULL) {
			unsigned char buf[2];		/*  gzip header  */
			memset(buf, 0, sizeof(buf));
			fread(buf, 1, sizeof(buf), tmp_f);
			if (buf[0]==0x1f && buf[1]==0x8b) {
				size_t zzlen = strlen(name_to_load)*2 + 100;
				char *zz = malloc(zzlen);
				debug("gunziping %s\n", name_to_load);
				/*
				 *  gzip header found.  If this was a file
				 *  extracted from, say, a CDROM image, then it
				 *  already has a temporary name. Otherwise we
				 *  have to gunzip into a temporary file.
				 */
				if (remove_after_load) {
					snprintf(zz, zzlen, "mv %s %s.gz",
					    name_to_load, name_to_load);
					system(zz);
					snprintf(zz, zzlen, "gunzip %s.gz",
					    name_to_load);
					system(zz);
				} else {
					/*  gunzip into new temp file:  */
					int tmpfile_handle;
					char *new_temp_name =
					    strdup("/tmp/gxemul.XXXXXXXXXXXX");
					tmpfile_handle = mkstemp(new_temp_name);
					close(tmpfile_handle);
					snprintf(zz, zzlen, "gunzip -c '%s' > "
					    "%s", name_to_load, new_temp_name);
					system(zz);
					name_to_load = new_temp_name;
					remove_after_load = 1;
				}
				free(zz);
			}
			fclose(tmp_f);
		}

		/*
		 *  Ugly (but usable) hack for Playstation Portable:  If the
		 *  filename ends with ".pbp" and the file contains an ELF
		 *  header, then extract the ELF file into a temporary file.
		 */
		if (strlen(name_to_load) > 4 && strcasecmp(name_to_load +
		    strlen(name_to_load) - 4, ".pbp") == 0 &&
		    (tmp_f = fopen(name_to_load, "r")) != NULL) {
			off_t filesize, j, found=0;
			unsigned char *buf;
			fseek(tmp_f, 0, SEEK_END);
			filesize = ftello(tmp_f);
			fseek(tmp_f, 0, SEEK_SET);
			buf = malloc(filesize);
			if (buf == NULL) {
				fprintf(stderr, "out of memory while trying"
				    " to read %s\n", name_to_load);
				exit(1);
			}
			fread(buf, 1, filesize, tmp_f);
			fclose(tmp_f);
			/*  Search for the ELF header, from offset 1 (!):  */
			for (j=1; j<filesize - 4; j++)
				if (memcmp(buf + j, ELFMAG, SELFMAG) == 0) {
					found = j;
					break;
				}
			if (found != 0) {
				int tmpfile_handle;
				char *new_temp_name =
				    strdup("/tmp/gxemul.XXXXXXXXXXXX");
				debug("extracting ELF from %s (offset 0x%x)\n",
				    name_to_load, (int)found);
				tmpfile_handle = mkstemp(new_temp_name);
				write(tmpfile_handle, buf + found,
				    filesize - found);
				close(tmpfile_handle);
				name_to_load = new_temp_name;
				remove_after_load = 1;
			}
		}

		/*  Special things required _before_ loading the file:  */
		switch (m->arch) {
		case ARCH_X86:
			/*
			 *  X86 machines normally don't need to load any files,
			 *  they can boot from disk directly. Therefore, an x86
			 *  machine usually boots up in 16-bit real mode. When
			 *  loading a 32-bit (or even 64-bit) ELF, that's not
			 *  very nice, hence this special case.
			 */
			pc_bios_simple_pmode_setup(cpu);
			break;
		}

		byte_order = NO_BYTE_ORDER_OVERRIDE;

		/*
		 *  Load the file:  :-)
		 */
		file_load(m, m->memory, name_to_load, &entrypoint,
		    m->arch, &gp, &byte_order, &toc);

		if (remove_after_load) {
			debug("removing %s\n", name_to_load);
			unlink(name_to_load);
		}

		if (byte_order != NO_BYTE_ORDER_OVERRIDE)
			cpu->byte_order = byte_order;

		cpu->pc = entrypoint;

		switch (m->arch) {

		case ARCH_ALPHA:
			/*  For position-independent code:  */
			cpu->cd.alpha.r[ALPHA_T12] = cpu->pc;
			break;

		case ARCH_ARM:
			if (cpu->pc & 3) {
				fatal("ARM: lowest bits of pc set: TODO\n");
				exit(1);
			}
			cpu->pc &= 0xfffffffc;
			break;

		case ARCH_AVR:
			cpu->pc &= 0xfffff;
			if (cpu->pc & 1) {
				fatal("AVR: lowest bit of pc set: TODO\n");
				exit(1);
			}
			break;

		case ARCH_HPPA:
			break;

		case ARCH_I960:
			break;

		case ARCH_IA64:
			break;

		case ARCH_M68K:
			break;

		case ARCH_MIPS:
			if ((cpu->pc >> 32) == 0 && (cpu->pc & 0x80000000ULL))
				cpu->pc |= 0xffffffff00000000ULL;

			cpu->cd.mips.gpr[MIPS_GPR_GP] = gp;

			if ((cpu->cd.mips.gpr[MIPS_GPR_GP] >> 32) == 0 &&
			    (cpu->cd.mips.gpr[MIPS_GPR_GP] & 0x80000000ULL))
				cpu->cd.mips.gpr[MIPS_GPR_GP] |=
				    0xffffffff00000000ULL;
			break;

		case ARCH_PPC:
			/*  See http://www.linuxbase.org/spec/ELF/ppc64/
			    spec/x458.html for more info.  */
			cpu->cd.ppc.gpr[2] = toc;
			/*  TODO  */
			if (cpu->cd.ppc.bits == 32)
				cpu->pc &= 0xffffffffULL;
			break;

		case ARCH_SH:
			if (cpu->cd.sh.bits == 32)
				cpu->pc &= 0xffffffffULL;
			cpu->pc &= ~1;
			break;

		case ARCH_SPARC:
			break;

		case ARCH_X86:
			/*
			 *  NOTE: The toc field is used to indicate an ELF32
			 *  or ELF64 load.
			 */
			switch (toc) {
			case 0:	/*  16-bit? TODO  */
				cpu->pc &= 0xffffffffULL;
				break;
			case 1:	/*  32-bit.  */
				cpu->pc &= 0xffffffffULL;
				break;
			case 2:	/*  64-bit:  TODO  */
				fatal("64-bit x86 load. TODO\n");
				exit(1);
			}
			break;

		default:
			fatal("emul_machine_setup(): Internal error: "
			    "Unimplemented arch %i\n", m->arch);
			exit(1);
		}

		/*
		 *  For userland emulation, the remaining items
		 *  on the command line will be passed as parameters
		 *  to the emulated program, and will not be treated
		 *  as filenames to load into the emulator.
		 *  The program's name will be in load_names[0], and the
		 *  rest of the parameters in load_names[1] and up.
		 */
		if (m->userland_emul != NULL)
			break;

		n_load --;
		load_names ++;
	}

	if (m->byte_order_override != NO_BYTE_ORDER_OVERRIDE)
		cpu->byte_order = m->byte_order_override;

	/*  Same byte order and entrypoint for all CPUs:  */
	for (i=0; i<m->ncpus; i++)
		if (i != m->bootstrap_cpu) {
			m->cpus[i]->byte_order = cpu->byte_order;
			m->cpus[i]->pc = cpu->pc;
		}

	if (m->userland_emul != NULL)
		useremul_setup(cpu, n_load, load_names);

	/*  Startup the bootstrap CPU:  */
	cpu->bootstrap_cpu_flag = 1;
	cpu->running            = 1;

	/*  ... or pause all CPUs, if start_paused is set:  */
	if (m->start_paused) {
		for (i=0; i<m->ncpus; i++)
			m->cpus[i]->running = 0;
	}

	/*  Add PC dump points:  */
	add_dump_points(m);

	/*  TODO: This is MIPS-specific!  */
	if (m->machine_type == MACHINE_PMAX &&
	    cpu->cd.mips.cpu_type.mmu_model == MMU3K)
		add_symbol_name(&m->symbol_context,
		    0x9fff0000, 0x10000, "r2k3k_cache", 0, 0);

	symbol_recalc_sizes(&m->symbol_context);

	/*  Special hack for ARC/SGI emulation:  */
	if ((m->machine_type == MACHINE_ARC ||
	    m->machine_type == MACHINE_SGI) && m->prom_emulation)
		add_arc_components(m);

	debug("starting cpu%i at ", m->bootstrap_cpu);
	switch (m->arch) {

	case ARCH_ARM:
		/*  ARM cpus aren't 64-bit:  */
		debug("0x%08x", (int)entrypoint);
		break;

	case ARCH_AVR:
		/*  Atmel AVR uses a 16-bit or 22-bit program counter:  */
		debug("0x%04x", (int)entrypoint);
		break;

	case ARCH_MIPS:
		if (cpu->is_32bit) {
			debug("0x%08x", (int)m->cpus[
			    m->bootstrap_cpu]->pc);
			if (cpu->cd.mips.gpr[MIPS_GPR_GP] != 0)
				debug(" (gp=0x%08x)", (int)m->cpus[
				    m->bootstrap_cpu]->cd.mips.gpr[
				    MIPS_GPR_GP]);
		} else {
			debug("0x%016llx", (long long)m->cpus[
			    m->bootstrap_cpu]->pc);
			if (cpu->cd.mips.gpr[MIPS_GPR_GP] != 0)
				debug(" (gp=0x%016llx)", (long long)
				    cpu->cd.mips.gpr[MIPS_GPR_GP]);
		}
		break;

	case ARCH_PPC:
		if (cpu->cd.ppc.bits == 32)
			debug("0x%08x", (int)entrypoint);
		else
			debug("0x%016llx", (long long)entrypoint);
		break;

	case ARCH_X86:
		debug("0x%04x:0x%llx", cpu->cd.x86.s[X86_S_CS],
		    (long long)cpu->pc);
		break;

	default:
		if (cpu->is_32bit)
			debug("0x%08x", (int)cpu->pc);
		else
			debug("0x%016llx", (long long)cpu->pc);
	}
	debug("\n");

	debug_indentation(-iadd);
}


/*
 *  emul_dumpinfo():
 *
 *  Dump info about all machines in an emul.
 */
void emul_dumpinfo(struct emul *e)
{
	int j, nm, iadd = DEBUG_INDENTATION;

	if (e->net != NULL)
		net_dumpinfo(e->net);

	nm = e->n_machines;
	for (j=0; j<nm; j++) {
		debug("machine %i: \"%s\"\n", j, e->machines[j]->name);
		debug_indentation(iadd);
		machine_dumpinfo(e->machines[j]);
		debug_indentation(-iadd);
	}
}


/*
 *  emul_simple_init():
 *
 *  For a normal setup:
 *
 *	o)  Initialize a network.
 *	o)  Initialize one machine.
 *
 *  For a userland-only setup:
 *
 *	o)  Initialize a "pseudo"-machine.
 */
void emul_simple_init(struct emul *emul)
{
	int iadd = DEBUG_INDENTATION;
	struct machine *m;

	if (emul->n_machines != 1) {
		fprintf(stderr, "emul_simple_init(): n_machines != 1\n");
		exit(1);
	}

	m = emul->machines[0];

	if (m->userland_emul == NULL) {
		debug("Simple setup...\n");
		debug_indentation(iadd);

		/*  Create a simple network:  */
		emul->net = net_init(emul, NET_INIT_FLAG_GATEWAY,
		    "10.0.0.0", 8, NULL, 0, 0);
	} else {
		/*  Userland pseudo-machine:  */
		debug("Syscall emulation (userland-only) setup...\n");
		debug_indentation(iadd);
	}

	/*  Create the machine:  */
	emul_machine_setup(m, extra_argc, extra_argv, 0, NULL);

	debug_indentation(-iadd);
}


/*
 *  emul_create_from_configfile():
 *
 *  Create an emul struct by reading settings from a configuration file.
 */
struct emul *emul_create_from_configfile(char *fname)
{
	int iadd = DEBUG_INDENTATION;
	struct emul *e = emul_new(fname);

	debug("Creating emulation from configfile \"%s\":\n", fname);
	debug_indentation(iadd);

	emul_parse_config(e, fname);

	debug_indentation(-iadd);
	return e;
}


/*
 *  emul_run():
 *
 *	o)  Set up things needed before running emulations.
 *
 *	o)  Run emulations (one or more, in parallel).
 *
 *	o)  De-initialize things.
 */
void emul_run(struct emul **emuls, int n_emuls)
{
	struct emul *e;
	int i = 0, j, go = 1, n, anything;

	if (n_emuls < 1) {
		fprintf(stderr, "emul_run(): no thing to do\n");
		return;
	}

	atexit(fix_console);

	/*  Initialize the interactive debugger:  */
	debugger_init(emuls, n_emuls);

	/*  Run any additional debugger commands before starting:  */
	for (i=0; i<n_emuls; i++) {
		struct emul *emul = emuls[i];
		if (emul->n_debugger_cmds > 0) {
			int j;
			if (i == 0)
				print_separator();
			for (j = 0; j < emul->n_debugger_cmds; j ++) {
				debug("> %s\n", emul->debugger_cmds[j]);
				debugger_execute_cmd(emul->debugger_cmds[j],
				    strlen(emul->debugger_cmds[j]));
			}
		}
	}

	print_separator();
	debug("\n");


	/*
	 *  console_init_main() makes sure that the terminal is in a
	 *  reasonable state.
	 *
	 *  The SIGINT handler is for CTRL-C  (enter the interactive debugger).
	 *
	 *  The SIGCONT handler is invoked whenever the user presses CTRL-Z
	 *  (or sends SIGSTOP) and then continues. It makes sure that the
	 *  terminal is in an expected state.
	 */
	console_init_main(emuls[0]);	/*  TODO: what is a good argument?  */
	signal(SIGINT, debugger_activate);
	signal(SIGCONT, console_sigcont);

	/*  Not in verbose mode? Then set quiet_mode.  */
	if (!verbose)
		quiet_mode = 1;

	/*  Initialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_init(e->machines[j]);
	}

	/*  TODO: Generalize:  */
	if (emuls[0]->machines[0]->show_trace_tree)
		cpu_functioncall_trace(emuls[0]->machines[0]->cpus[0],
		    emuls[0]->machines[0]->cpus[0]->pc);

	/*
	 *  MAIN LOOP:
	 *
	 *  Run all emulations in parallel, running each machine in
	 *  each emulation.
	 */
	while (go) {
		go = 0;

		/*  Flush X11 and serial console output every now and then:  */
		if (emuls[0]->machines[0]->ninstrs >
		    emuls[0]->machines[0]->ninstrs_flush + (1<<18)) {
			x11_check_event(emuls, n_emuls);
			console_flush();
			emuls[0]->machines[0]->ninstrs_flush =
			    emuls[0]->machines[0]->ninstrs;
		}

		if (emuls[0]->machines[0]->ninstrs >
		    emuls[0]->machines[0]->ninstrs_show + (1<<25)) {
			emuls[0]->machines[0]->ninstrs_since_gettimeofday +=
			    (emuls[0]->machines[0]->ninstrs -
			     emuls[0]->machines[0]->ninstrs_show);
			cpu_show_cycles(emuls[0]->machines[0], 0);
			emuls[0]->machines[0]->ninstrs_show =
			    emuls[0]->machines[0]->ninstrs;
		}

		if (single_step == ENTER_SINGLE_STEPPING) {
			/*  TODO: Cleanup!  */
			old_instruction_trace =
			    emuls[0]->machines[0]->instruction_trace;
			old_quiet_mode = quiet_mode;
			old_show_trace_tree =
			    emuls[0]->machines[0]->show_trace_tree;
			emuls[0]->machines[0]->instruction_trace = 1;
			emuls[0]->machines[0]->show_trace_tree = 1;
			quiet_mode = 0;
			single_step = SINGLE_STEPPING;
		}

		if (single_step == SINGLE_STEPPING)
			debugger();

		e = emuls[0];	/*  Note: Only 1 emul supported now.  */

		for (j=0; j<e->n_machines; j++) {
			if (e->machines[j]->gdb.port > 0)
				debugger_gdb_check_incoming(e->machines[j]);

			anything = machine_run(e->machines[j]);
			if (anything)
				go = 1;
		}
	}

	/*  Deinitialize all CPUs in all machines in all emulations:  */
	for (i=0; i<n_emuls; i++) {
		e = emuls[i];
		if (e == NULL)
			continue;
		for (j=0; j<e->n_machines; j++)
			cpu_run_deinit(e->machines[j]);
	}

	/*  force_debugger_at_exit flag set? Then enter the debugger:  */
	if (force_debugger_at_exit) {
		quiet_mode = 0;
		debugger_reset();
		debugger();
	}

	/*  Any machine using X11? Then we should wait before exiting:  */
	n = 0;
	for (i=0; i<n_emuls; i++)
		for (j=0; j<emuls[i]->n_machines; j++)
			if (emuls[i]->machines[j]->use_x11)
				n++;
	if (n > 0) {
		printf("Press enter to quit.\n");
		while (!console_charavail(MAIN_CONSOLE)) {
			x11_check_event(emuls, n_emuls);
			usleep(1);
		}
		console_readchar(MAIN_CONSOLE);
	}

	console_deinit();
}


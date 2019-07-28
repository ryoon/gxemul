/*  Imported into GXmeul 2018-04-21. Some fields renamed and data types changed.  */

#ifndef EXEC_B_OUT_H
#define	EXEC_B_OUT_H

/*(c****************************************************************************** *
 * Copyright (c) 1990, 1991, 1992, 1993 Intel Corporation
 * 
 * Intel hereby grants you permission to copy, modify, and distribute this
 * software and its documentation.  Intel grants this permission provided
 * that the above copyright notice appears in all copies and that both the
 * copyright notice and this permission notice appear in supporting
 * documentation.  In addition, Intel grants this permission provided that
 * you prominently mark as "not part of the original" any modifications
 * made to this software or documentation, and that the name of Intel
 * Corporation not be used in advertising or publicity pertaining to
 * distribution of the software or the documentation without specific,
 * written prior permission.
 * 
 * Intel Corporation provides this AS IS, WITHOUT ANY WARRANTY, EXPRESS OR
 * IMPLIED, INCLUDING, WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY
 * OR FITNESS FOR A PARTICULAR PURPOSE.  Intel makes no guarantee or
 * representations regarding the use of, or the results of the use of,
 * the software and documentation in terms of correctness, accuracy,
 * reliability, currentness, or otherwise; and you rely on the software,
 * documentation and results solely at your own risk.
 * 
 * IN NO EVENT SHALL INTEL BE LIABLE FOR ANY LOSS OF USE, LOSS OF BUSINESS,
 * LOSS OF PROFITS, INDIRECT, INCIDENTAL, SPECIAL OR CONSEQUENTIAL DAMAGES
 * OF ANY KIND.  IN NO EVENT SHALL INTEL'S TOTAL LIABILITY EXCEED THE SUM
 * PAID TO INTEL FOR THE PRODUCT LICENSED HEREUNDER.
 * 
 *****************************************************************************c)*/

/*
 * This file, a modified version of 'a.out.h', describes the 'b.out' format
 * produced by GNU tools modified to support the i960 processor.
 *
 * All i960 development is done in a CROSS-DEVELOPMENT environment.  I.e.,
 * object code is generated on, and executed under the direction of a symbolic
 * debugger running on, a host system.  We do not want to be subject to the
 * vagaries of which host it is or whether it supports COFF or a.out format,
 * or anything else.  We DO want to:
 *
 *	o always generate the same format object files, regardless of host.
 *
 *	o provide support for additional linker features that the normal
 *	  a.out.h header can't accommodate.
 *
 * As for byte-ordering, the following rules apply:
 *
 *	o Text and data contents (which are actually downloaded to the target)
 *	  is always in i960 (little-endian) order.
 *
 *	o Other binary numbers in the file (in the header, symbols, relocation
 *	  directives) are either all big-endian or all little-endian.  It used
 *	  to be dependent on the host on which the file was created, but we
 *	  are moving toward a tool set that accepts either order on input and
 *	  always generates little-endian output.
 *
 *	o The downloader (comm960 or gdb960) copies a b.out file to a tmp file
 *	  and converts it into a stripped little-endian COFF file before
 *	  shipping it off to the NINDY monitor in the target systems.  If you
 *	  use a different downloader and want to send a b.out file to NINDY,
 *	  you should perform the conversion explicitly -- see the objcopy
 *	  utility.
 */


#define BOUT_BMAGIC	0415
/* We don't accept the following (see N_BADMAG macro).
 * They're just here so GNU code will compile.
 */
#define	BOUT_OMAGIC	0407		/* old impure format */
#define	BOUT_NMAGIC	0410		/* read-only text */
#define	BOUT_ZMAGIC	0413		/* demand load format */

/* FILE HEADER
 *	All 'lengths' are given as a number of bytes.
 *	All 'alignments' are for relinkable files only;  an alignment of
 *		'n' indicates the corresponding segment must begin at an
 *		address that is a multiple of (2**n).
 */
struct bout_exec {
	/* Standard stuff */
	uint32_t a_magic;	/* Identifies this as a b.out file	*/
	uint32_t a_text;	/* Length of text			*/
	uint32_t a_data;	/* Length of data			*/
	uint32_t a_bss;	/* Length of runtime uninitialized data area */
	uint32_t a_syms;	/* Length of symbol table		*/
	uint32_t a_entry;	/* Runtime start address		*/
	uint32_t a_trsize;	/* Length of text relocation info	*/
	uint32_t a_drsize;	/* Length of data relocation info	*/

	/* Added for i960 */
	uint32_t a_tload;	/* Text runtime load address		*/
	uint32_t a_dload;	/* Data runtime load address		*/
	uint8_t  a_talign;	/* Alignment of text segment		*/
	uint8_t  a_dalign;	/* Alignment of data segment		*/
	uint8_t  a_balign;	/* Alignment of bss segment		*/
	uint8_t  a_ccinfo;	/* See below				*/
};

/* The field a_ccinfo contains the magic value N_CCINFO iff cc_info data
 * (for 2-pass compiler optimization) is appended to the end of the object file.
 *
 * Since cc_info data is removed when a file is stripped, we can assume that
 * its presence implies the presence of a string table in the file, with the
 * cc_info block immediately following.
 *
 * The format/meaning of the cc_data block are known only to the compiler (and,
 * to a lesser extent, the linker) except for the first 4 bytes, which contain
 * the length of the block (including those 4 bytes).  This length is stored in
 * a machine-independent format, and can be retrieved with the CI_U32_FM_BUF
 * macro in cc_info.h .
 */

#define N_MAGIC(x)	((x).a_magic)
#define N_BADMAG(x)	(((x).a_magic)!=BOUT_BMAGIC)
#define N_TXTOFF(x)	( sizeof(struct exec) )
#define N_DATOFF(x)	( N_TXTOFF(x) + (x).a_text )
#define N_TROFF(x)	( N_DATOFF(x) + (x).a_data )
#define N_DROFF(x)	( N_TROFF(x) + (x).a_trsize )
#define N_SYMOFF(x)	( N_DROFF(x) + (x).a_drsize )
#define N_STROFF(x)	( N_SYMOFF(x) + (x).a_syms )
#define N_CCINFO	( 0x17 )
#define N_HAS_CCINFO(x)	(((x).a_ccinfo)==N_CCINFO)

/* A single entry in the symbol table
 */
struct nlist {
	union {
		char	*n_name;
		struct nlist *n_next;
		long	n_strx;		/* Index into string table	*/
	} n_un;
	unsigned char n_type;	/* See below				*/
	char	n_other;	/* Used in i960 support -- see below	*/
	short	n_desc;
	unsigned long n_value;
};


/* Legal values of n_type
 */
#define N_UNDF	0	/* Undefined symbol	*/
#define N_ABS	2	/* Absolute symbol	*/
#define N_TEXT	4	/* Text symbol		*/
#define N_DATA	6	/* Data symbol		*/
#define N_BSS	8	/* BSS symbol		*/
#define N_FN	31	/* Filename symbol	*/

#define N_EXT	1	/* External symbol (OR'd in with one of above)	*/
#define N_TYPE	036	/* Mask for all the type bits			*/
#define N_STAB	0340	/* Mask for all bits used for SDB entries 	*/

/* MEANING OF 'n_other'
 *
 * If n_other is 0, it means the symbol is an ordinary aout symbol.
 *
 * If non-zero, the 'n_other' fields indicates either a leaf procedure or
 * a system procedure, as follows:
 *
 *	1 <= n_other < N_BALNAME :
 *		The symbol is the entry point to a system procedure.
 *		'n_value' is the address of the entry, as for any other
 *		procedure.  The system procedure number (which can be used in
 *		a 'calls' instruction) is (n_other-1).  These entries come from
 *		'.sysproc' directives.
 *
 *	n_other == N_CALLNAME
 *		the symbol is the 'call' entry point to a leaf procedure.
 *		The *next* symbol in the symbol table must be the corresponding
 *		'bal' entry point to the procedure (see following).  These
 *		entries come from '.leafproc' directives in which two different
 *		symbols are specified (the first one is represented here).
 *	
 *
 *	n_other == N_BALNAME
 *		the symbol is the 'bal' entry point to a leaf procedure.
 *		These entries result from '.leafproc' directives in which only
 *		one symbol is specified, or in which the same symbol is
 *		specified twice.
 *
 * Note that an N_CALLNAME entry *must* have a corresponding N_BALNAME entry,
 * but not every N_BALNAME entry must have an N_CALLNAME entry.
 */
#define N_ORDINARY       ((unsigned) 0)

#define N_BALNAME	 ((unsigned) 0xfe)
#define N_CALLNAME	 ((unsigned) 0xff)

#define MASK( V ) ((sizeof(V) == 1) ? 0x000000ff :\
		  ((sizeof(V) == 2) ? 0x0000ffff :\
		                      0x0000ffff))

/* Get unsigned bits: */
#define GET_UBITS( V ) ((unsigned) (V & MASK( V )))

#define IS_ORDINARY(x)   (GET_UBITS(x) == N_ORDINARY)
#define IS_CALLNAME(x)	 (GET_UBITS(x) == N_CALLNAME)
#define IS_BALNAME(x)	 (GET_UBITS(x) == N_BALNAME) 
#define IS_SYSPROCIDX(x) (GET_UBITS(x) > N_ORDINARY &&\
			  GET_UBITS(x) < N_BALNAME)

/*
 * Note that the following data structure won't compile using a 16-bit 
 * compiler (MSC7, for example).  But is not needed for the target client
 * (mondb).  So just ifdef the structure out of existence...
 */
#ifndef CC_16BIT
struct relocation_info {
	int	 r_address;	/* File address of item to be relocated	*/
	unsigned
		r_symbolnum:24,/* Index of symbol on which relocation is based,
				*	if r_extern is set.  Otherwise set to
				*	either N_TEXT, N_DATA, or N_BSS to
				*	indicate section on which relocation is
				*	based.
				*/
		r_pcrel:1,	/* 1 => relocate PC-relative; else absolute
				 *	On i960, pc-relative implies 24-bit
				 *	address, absolute implies 32-bit.
				 */
		r_length:2,	/* Number of bytes to relocate:
				 *	0 => 1 byte
				 *	1 => 2 bytes
				 *	2 => 4 bytes -- only value used for i960
				 */
		r_extern:1,
		r_bsr:1,	/* Something for the GNU NS32K assembler */
		r_disp:1,	/* Something for the GNU NS32K assembler */
		r_callj:1,	/* 1 if relocation target is an i960 'callj' */
		r_calljx:1;	/* 1 if relocation target is an i960 'calljx' */
};
#endif

#endif	// EXEC_B_OUT_H


#ifndef	MEMORY_H
#define	MEMORY_H

/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: memory.h,v 1.41 2005-11-11 07:31:33 debug Exp $
 *
 *  Memory controller related functions.
 */

#include <sys/types.h>
#include <inttypes.h>

#include "misc.h"


#define	DEFAULT_RAM_IN_MB		32
#define	MAX_DEVICES			24

#define	DEVICE_STATE_TYPE_INT		1
#define	DEVICE_STATE_TYPE_UINT64_T	2

struct cpu;
struct translation_page_entry;

/*  For bintrans:  */
#define	MAX_QUICK_JUMPS			8

struct memory {
	uint64_t	physical_max;
	void		*pagetable;

	int		n_mmapped_devices;
	int		last_accessed_device;
	/*  The following two might speed up things a little bit.  */
	/*  (actually maxaddr is the addr after the last address)  */
	uint64_t	mmap_dev_minaddr;
	uint64_t	mmap_dev_maxaddr;

	const char	*dev_name[MAX_DEVICES];
	uint64_t	dev_baseaddr[MAX_DEVICES];
	uint64_t	dev_endaddr[MAX_DEVICES];	/*  after the end!  */
	uint64_t	dev_length[MAX_DEVICES];
	int		dev_flags[MAX_DEVICES];
	void		*dev_extra[MAX_DEVICES];
	int		(*dev_f[MAX_DEVICES])(struct cpu *,struct memory *,
			    uint64_t,unsigned char *,size_t,int,void *);
	int		(*dev_f_state[MAX_DEVICES])(struct cpu *,
			    struct memory *, void *extra, int wf, int nr,
			    int *type, char **namep, void **data, size_t *len);
	unsigned char	*dev_dyntrans_data[MAX_DEVICES];

	int		dev_dyntrans_alignment;

	uint64_t	dev_dyntrans_write_low[MAX_DEVICES];
	uint64_t	dev_dyntrans_write_high[MAX_DEVICES];


	/*
	 *  NOTE/TODO: This bintrans was for MIPS only. Ugly. :-/
	 */

	/*
	 *  translation_code_chunk_space is a large chunk of (linear) memory
	 *  where translated code chunks and translation_entrys are stored.
	 *  When this is filled, translation is restart from scratch (by
	 *  resetting translation_code_chunk_space_head to 0, and removing all
	 *  translation entries).
	 *
	 *  (Using a static memory region like this is somewhat inspired by
	 *  the QEMU web pages,
	 *  http://fabrice.bellard.free.fr/qemu/qemu-tech.html#SEC13)
	 */

	unsigned char	*translation_code_chunk_space;
	size_t		translation_code_chunk_space_head;

	int		bintrans_32bit_only;

	struct translation_page_entry **translation_page_entry_array;

	unsigned char	*quick_jump_host_address[MAX_QUICK_JUMPS];
	int		quick_jump_page_offset[MAX_QUICK_JUMPS];
	int		n_quick_jumps;
	int		quick_jumps_index;
};

#define	BITS_PER_PAGETABLE	20
#define	BITS_PER_MEMBLOCK	20
#define	MAX_BITS		40


/*  memory.c:  */
uint64_t memory_readmax64(struct cpu *cpu, unsigned char *buf, int len);
void memory_writemax64(struct cpu *cpu, unsigned char *buf, int len,
	uint64_t data);

void *zeroed_alloc(size_t s);

struct memory *memory_new(uint64_t physical_max, int arch);

int memory_points_to_string(struct cpu *cpu, struct memory *mem,
	uint64_t addr, int min_string_length);
char *memory_conv_to_string(struct cpu *cpu, struct memory *mem,
	uint64_t addr, char *buf, int bufsize);

unsigned char *memory_paddr_to_hostaddr(struct memory *mem,
	uint64_t paddr, int writeflag);

/*  memory_fast_v2h.c:  */
unsigned char *fast_vaddr_to_hostaddr(struct cpu *cpu, uint64_t vaddr,
	int writeflag);

/*  MIPS stuff:  */
int translate_address_mmu3k(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);
int translate_address_mmu8k(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);
int translate_address_mmu10k(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);
int translate_address_mmu4100(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);
int translate_address_generic(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);


/*
 *  Bit flags:
 */
#define	MEM_READ			0
#define	MEM_WRITE			1
#define	MEM_DOWNGRADE			128

#define	CACHE_DATA			0
#define	CACHE_INSTRUCTION		1
#define	CACHE_NONE			2

#define	CACHE_FLAGS_MASK		0x3

#define	NO_EXCEPTIONS			16
#define	PHYSICAL			32
#define	NO_SEGMENTATION			64	/*  for X86  */
#define	MEMORY_USER_ACCESS		128	/*  for ARM, at least  */

/*  Dyntrans flags:  */
#define	MEM_DEFAULT				0
#define	MEM_DYNTRANS_OK				1
#define	MEM_DYNTRANS_WRITE_OK			2
#define	MEM_READING_HAS_NO_SIDE_EFFECTS		4
#define	MEM_EMULATED_RAM			8

#define FLAG_WRITEFLAG          1
#define FLAG_NOEXCEPTIONS       2
#define FLAG_INSTR              4

int userland_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache);
#define	MEMORY_ACCESS_FAILED		0
#define	MEMORY_ACCESS_OK		1
#define	MEMORY_ACCESS_OK_WRITE		2
#define	MEMORY_NOT_FULL_PAGE		256

void memory_device_dyntrans_access(struct cpu *, struct memory *mem,
	void *extra, uint64_t *low, uint64_t *high);

void memory_device_register_statefunction(
	struct memory *mem, void *extra,
	int (*dev_f_state)(struct cpu *,
	    struct memory *, void *extra, int wf, int nr,
	    int *type, char **namep, void **data, size_t *len));

void memory_device_register(struct memory *mem, const char *,
	uint64_t baseaddr, uint64_t len, int (*f)(struct cpu *,
	    struct memory *,uint64_t,unsigned char *,size_t,int,void *),
	void *extra, int flags, unsigned char *dyntrans_data);
void memory_device_remove(struct memory *mem, int i);

#endif	/*  MEMORY_H  */

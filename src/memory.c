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
 *  $Id: memory.c,v 1.150 2005-01-30 00:37:09 debug Exp $
 *
 *  Functions for handling the memory of an emulated machine.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "bintrans.h"
#include "cop0.h"
#include "memory.h"
#include "misc.h"
#include "mips_cpu.h"


extern int quiet_mode;


/*
 *  memory_readmax64():
 *
 *  Read at most 64 bits of data from a buffer.  Length is given by
 *  len, and the byte order by cpu->byte_order.
 *
 *  This function should not be called with cpu == NULL.
 *
 *  TODO:  Maybe this shouldn't be in memory.c.  It's a kind of 'misc'
 *  helper function.
 */
uint64_t memory_readmax64(struct cpu *cpu, unsigned char *buf, int len)
{
	int i;
	uint64_t x = 0;

#if 0
	if (len > 8) {
		fatal("memory_readmax64(): len = %i\n", len);
		exit(1);
	}
#endif

	/*  Switch byte order for incoming data, if necessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			x <<= 8;
			x |= buf[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			x <<= 8;
			x |= buf[i];
		}

	return x;
}


/*
 *  memory_writemax64():
 *
 *  Write at most 64 bits of data to a buffer.  Length is given by
 *  len, and the byte order by cpu->byte_order.
 *
 *  This function should not be called with cpu == NULL.
 *
 *  TODO:  Maybe this shouldn't be in memory.c.  It's a kind of 'misc'
 *  helper function.
 */
void memory_writemax64(struct cpu *cpu, unsigned char *buf, int len,
	uint64_t data)
{
	int i;

#if 0
	if (len > 8) {
		fatal("memory_readmax64(): len = %i\n", len);
		exit(1);
	}
#endif

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		for (i=0; i<len; i++) {
			buf[i] = data & 255;
			data >>= 8;
		}
	} else {
		for (i=0; i<len; i++) {
			buf[len - 1 - i] = data & 255;
			data >>= 8;
		}
	}
}


/*
 *  zeroed_alloc():
 *
 *  Allocates a block of memory using mmap(), and if that fails, try
 *  malloc() + memset().
 */
void *zeroed_alloc(size_t s)
{
	void *p = mmap(NULL, s, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == NULL) {
		p = malloc(s);
		if (p == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(p, 0, s);
	}
	return p;
}


/*
 *  memory_new():
 *
 *  This function creates a new memory object. An emulated machine needs one
 *  of these.
 */
struct memory *memory_new(uint64_t physical_max)
{
	struct memory *mem;
	int bits_per_pagetable = BITS_PER_PAGETABLE;
	int bits_per_memblock = BITS_PER_MEMBLOCK;
	int entries_per_pagetable = 1 << BITS_PER_PAGETABLE;
	int max_bits = MAX_BITS;
	size_t s;

	mem = malloc(sizeof(struct memory));
	if (mem == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(mem, 0, sizeof(struct memory));

	/*  Check bits_per_pagetable and bits_per_memblock for sanity:  */
	if (bits_per_pagetable + bits_per_memblock != max_bits) {
		fprintf(stderr, "memory_new(): bits_per_pagetable and bits_per_memblock mismatch\n");
		exit(1);
	}

	mem->physical_max = physical_max;

	s = entries_per_pagetable * sizeof(void *);

	mem->pagetable = (unsigned char *) mmap(NULL, s,
	    PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (mem->pagetable == NULL) {
		mem->pagetable = malloc(s);
		if (mem->pagetable == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memset(mem->pagetable, 0, s);
	}

	mem->mmap_dev_minaddr = 0xffffffffffffffffULL;
	mem->mmap_dev_maxaddr = 0;

	return mem;
}


/*
 *  memory_points_to_string():
 *
 *  Returns 1 if there's something string-like at addr, otherwise 0.
 */
int memory_points_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr,
	int min_string_length)
{
	int cur_length = 0;
	unsigned char c;

	for (;;) {
		c = '\0';
		memory_rw(cpu, mem, addr+cur_length, &c, sizeof(c), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);
		if (c=='\n' || c=='\t' || c=='\r' || (c>=' ' && c<127)) {
			cur_length ++;
			if (cur_length >= min_string_length)
				return 1;
		} else {
			if (cur_length >= min_string_length)
				return 1;
			else
				return 0;
		}
	}
}


/*
 *  memory_conv_to_string():
 *
 *  Convert virtual memory contents to a string, placing it in a
 *  buffer provided by the caller.
 */
char *memory_conv_to_string(struct cpu *cpu, struct memory *mem, uint64_t addr,
	char *buf, int bufsize)
{
	int len = 0;
	int output_index = 0;
	unsigned char c, p='\0';

	while (output_index < bufsize-1) {
		c = '\0';
		memory_rw(cpu, mem, addr+len, &c, sizeof(c), MEM_READ,
		    CACHE_NONE | NO_EXCEPTIONS);
		buf[output_index] = c;
		if (c>=' ' && c<127) {
			len ++;
			output_index ++;
		} else if (c=='\n' || c=='\r' || c=='\t') {
			len ++;
			buf[output_index] = '\\';
			output_index ++;
			switch (c) {
			case '\n':	p = 'n'; break;
			case '\r':	p = 'r'; break;
			case '\t':	p = 't'; break;
			}
			if (output_index < bufsize-1) {
				buf[output_index] = p;
				output_index ++;
			}
		} else {
			buf[output_index] = '\0';
			return buf;
		}
	}

	buf[bufsize-1] = '\0';
	return buf;
}


/*
 *  insert_into_tiny_cache():
 *
 *  If the tiny cache is enabled (USE_TINY_CACHE), then this routine inserts
 *  a vaddr to paddr translation first in the instruction (or data) tiny
 *  translation cache.
 */
static void insert_into_tiny_cache(struct cpu *cpu, int instr, int writeflag,
	uint64_t vaddr, uint64_t paddr)
{
#ifdef USE_TINY_CACHE
	int wf = 1 + (writeflag == MEM_WRITE);

	if (cpu->machine->bintrans_enable)
		return;

	paddr &= ~0xfff;
	vaddr >>= 12;

	if (instr) {
		/*  Code:  */
		memmove(&cpu->translation_cache_instr[1],
		    &cpu->translation_cache_instr[0],
		    sizeof(struct translation_cache_entry) *
		    (N_TRANSLATION_CACHE_INSTR - 1));

		cpu->translation_cache_instr[0].wf = wf;
		cpu->translation_cache_instr[0].vaddr_pfn = vaddr;
		cpu->translation_cache_instr[0].paddr = paddr;
	} else {
		/*  Data:  */
		memmove(&cpu->translation_cache_data[1],
		    &cpu->translation_cache_data[0],
		    sizeof(struct translation_cache_entry) *
		    (N_TRANSLATION_CACHE_DATA - 1));

		cpu->translation_cache_data[0].wf = wf;
		cpu->translation_cache_data[0].vaddr_pfn = vaddr;
		cpu->translation_cache_data[0].paddr = paddr;
	}
#endif
}


/*
 *  memory_paddr_to_hostaddr():
 *
 *  Translate a physical MIPS address into a host address.
 *  Return value is a pointer to a host memblock, or NULL on failure.
 *  On reads, a NULL return value should be interpreted as reading all zeroes.
 */
unsigned char *memory_paddr_to_hostaddr(struct memory *mem,
	uint64_t paddr, int writeflag)
{
	void **table;
	int entry;
	const int mask = (1 << BITS_PER_PAGETABLE) - 1;
	const int shrcount = MAX_BITS - BITS_PER_PAGETABLE;

	table = mem->pagetable;
	entry = (paddr >> shrcount) & mask;

	/*  printf("   entry = %x\n", entry);  */

	if (table[entry] == NULL) {
		size_t alloclen;

		/*
		 *  Special case:  reading from a nonexistant memblock
		 *  returns all zeroes, and doesn't allocate anything.
		 *  (If any intermediate pagetable is nonexistant, then
		 *  the same thing happens):
		 */
		if (writeflag == MEM_READ)
			return NULL;

		/*  Allocate a memblock:  */
		alloclen = 1 << BITS_PER_MEMBLOCK;

		/*  printf("  allocating for entry %i, len=%i\n",
		    entry, alloclen);  */

		/*  Anonymous mmap() should return zero-filled memory,
		    try malloc + memset if mmap failed.  */
		table[entry] = (void *) mmap(NULL, alloclen,
		    PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
		    -1, 0);
		if (table[entry] == NULL) {
			table[entry] = malloc(alloclen);
			if (table[entry] == NULL) {
				fatal("out of memory\n");
				exit(1);
			}
			memset(table[entry], 0, alloclen);
		}
	}

	return (unsigned char *) table[entry];
}


/*
 *  memory_cache_R3000():
 *
 *  R2000/R3000 specific cache handling.
 *
 *  Return value is 1 if a jump to do_return_ok is supposed to happen directly
 *  after this routine is finished, 0 otherwise.
 */
int memory_cache_R3000(struct cpu *cpu, int cache, uint64_t paddr,
	int writeflag, size_t len, unsigned char *data)
{
#ifdef ENABLE_CACHE_EMULATION
	struct r3000_cache_line *rp;
	int cache_line;
	uint32_t tag_mask;
	unsigned char *memblock;
	struct memory *mem = cpu->mem;
	int offset;
#endif
	unsigned int i;
	int cache_isolated = 0, addr, hit, which_cache = cache;


	if (len > 4 || cache == CACHE_NONE)
		return 0;


#ifdef ENABLE_CACHE_EMULATION
	if (cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_SWAP_CACHES)
		which_cache ^= 1;

	tag_mask = 0xffffffff & ~cpu->cache_mask[which_cache];
	cache_line = (paddr & cpu->cache_mask[which_cache])
	    / cpu->cache_linesize[which_cache];
	rp = (struct r3000_cache_line *) cpu->cache_tags[which_cache];

	/*  Is this a cache hit or miss?  */
	hit = (rp[cache_line].tag_valid & R3000_TAG_VALID) &&
	    (rp[cache_line].tag_paddr == (paddr & tag_mask));

#ifdef ENABLE_INSTRUCTION_DELAYS
	if (!hit)
		cpu->instruction_delay += cpu->cpu_type.instrs_per_cycle
		    * cpu->cache_miss_penalty[which_cache];
#endif

	/*
	 *  The cache miss bit is only set on cache reads, and only to the
	 *  data cache. (?)
	 *
	 *  (TODO: is this correct? I don't remember where I got this from.)
	 */
	if (cache == CACHE_DATA && writeflag==MEM_READ) {
		cpu->coproc[0]->reg[COP0_STATUS] &= ~MIPS1_CACHE_MISS;
		if (!hit)
			cpu->coproc[0]->reg[COP0_STATUS] |= MIPS1_CACHE_MISS;
	}

	/*
	 *  Is the Data cache isolated?  Then don't access main memory:
	 */
	if (cache == CACHE_DATA &&
	    cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES)
		cache_isolated = 1;

	addr = paddr & cpu->cache_mask[which_cache];

	/*
	 *  If there was a miss and the cache is not isolated, then flush
	 *  the old cacheline back to main memory, and read in the new
	 *  cacheline.
	 *
	 *  Then access the cache.
	 */
/*
	fatal("L1 CACHE isolated=%i hit=%i write=%i cache=%i cacheline=%i"
	    " paddr=%08x => addr in"
	    " cache = 0x%lx\n", cache_isolated, hit, writeflag,
	    which_cache, cache_line, (int)paddr,
	    addr);
*/
	if (!hit && !cache_isolated) {
		unsigned char *dst, *src;
		uint64_t old_cached_paddr = rp[cache_line].tag_paddr
		    + cache_line * cpu->cache_linesize[which_cache];

		/*  Flush the old cacheline to main memory:  */
		if ((rp[cache_line].tag_valid & R3000_TAG_VALID) &&
		    (rp[cache_line].tag_valid & R3000_TAG_DIRTY)) {
/*			fatal("  FLUSHING old tag=0%08x "
			    "old_cached_paddr=0x%08x\n",
			    rp[cache_line].tag_paddr,
			    old_cached_paddr);
*/
			memblock = memory_paddr_to_hostaddr(
			    mem, old_cached_paddr, MEM_WRITE);
			offset = old_cached_paddr
			    & ((1 << BITS_PER_MEMBLOCK) - 1)
			    & ~cpu->cache_mask[which_cache];

			src = cpu->cache[which_cache];
			dst = memblock + (offset &
			    ~cpu->cache_mask[which_cache]);

			src += cache_line *
			    cpu->cache_linesize[which_cache];
			dst += cache_line *
			    cpu->cache_linesize[which_cache];

			if (memblock == NULL) {
				fatal("BUG in memory.c! Hm.\n");
			} else {
				memcpy(dst, src,
				    cpu->cache_linesize[which_cache]);
			}
			/*  offset is the offset within
			 *  the memblock:
			 *  printf("read: offset = 0x%x\n", offset);
			 */
		}

		/*  Copy from main memory into the cache:  */
		memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
		offset = paddr & ((1 << BITS_PER_MEMBLOCK) - 1)
		    & ~cpu->cache_mask[which_cache];
		/*  offset is offset within the memblock:
		 *  printf("write: offset = 0x%x\n", offset);
		 */

/*		fatal("  FETCHING new paddr=0%08x\n", paddr);
*/
		dst = cpu->cache[which_cache];

		if (memblock == NULL) {
			if (writeflag == MEM_READ)
			memset(dst, 0, cpu->cache_linesize[which_cache]);
		} else {
			src = memblock + (offset &
			    ~cpu->cache_mask[which_cache]);

			src += cache_line *
			    cpu->cache_linesize[which_cache];
			dst += cache_line *
			    cpu->cache_linesize[which_cache];
			memcpy(dst, src, cpu->cache_linesize[which_cache]);
		}

		rp[cache_line].tag_paddr = paddr & tag_mask;
		rp[cache_line].tag_valid = R3000_TAG_VALID;
	}

	if (cache_isolated && writeflag == MEM_WRITE) {
		rp[cache_line].tag_valid = 0;
	}

	if (writeflag==MEM_READ) {
		for (i=0; i<len; i++)
			data[i] = cpu->cache[which_cache][(addr+i) &
			    cpu->cache_mask[which_cache]];
	} else {
		for (i=0; i<len; i++) {
			if (cpu->cache[which_cache][(addr+i) &
			    cpu->cache_mask[which_cache]] != data[i]) {
				rp[cache_line].tag_valid |= R3000_TAG_DIRTY;
			}
			cpu->cache[which_cache][(addr+i) &
			    cpu->cache_mask[which_cache]] = data[i];
		}
	}

	/*  Run instructions from the right host page:  */
	if (cache == CACHE_INSTRUCTION) {
		memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
		if (memblock != NULL) {
			cpu->pc_last_host_4k_page = memblock +
			    (paddr & ((1 << BITS_PER_MEMBLOCK) - 1) & ~0xfff);
		}
	}

	/*  Write-through! (Write to main memory as well.)  */
	if (writeflag == MEM_READ || cache_isolated)
		return 1;

#else

	/*
	 *  R2000/R3000 without correct cache emulation:
	 *
	 *  TODO: This is just enough to trick NetBSD/pmax and Ultrix into
	 *  being able to detect the cache sizes and think that the caches
	 *  are actually working, but they are not.
	 */

	if (cache != CACHE_DATA)
		return 0;

	/*  Is this a cache hit or miss?  */
	hit = (cpu->cache_last_paddr[which_cache]
		& ~cpu->cache_mask[which_cache])
	    == (paddr & ~(cpu->cache_mask[which_cache]));

#ifdef ENABLE_INSTRUCTION_DELAYS
	if (!hit)
		cpu->instruction_delay += cpu->cpu_type.instrs_per_cycle
		    * cpu->cache_miss_penalty[which_cache];
#endif

	/*
	 *  The cache miss bit is only set on cache reads, and only to the
	 *  data cache. (?)
	 *
	 *  (TODO: is this correct? I don't remember where I got this from.)
	 */
	if (cache == CACHE_DATA && writeflag==MEM_READ) {
		cpu->coproc[0]->reg[COP0_STATUS] &= ~MIPS1_CACHE_MISS;
		if (!hit)
			cpu->coproc[0]->reg[COP0_STATUS] |= MIPS1_CACHE_MISS;
	}

	/*
	 *  Is the Data cache isolated?  Then don't access main memory:
	 */
	if (cache == CACHE_DATA &&
	    cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_ISOL_CACHES)
		cache_isolated = 1;

	addr = paddr & cpu->cache_mask[which_cache];

	/*  Data cache isolated?  Then don't access main memory:  */
	if (cache_isolated) {
		/*  debug("ISOLATED write=%i cache=%i vaddr=%016llx paddr=%016llx => addr in cache = 0x%lx\n",
		    writeflag, cache, (long long)vaddr, (long long)paddr, addr);  */

		if (writeflag==MEM_READ) {
			for (i=0; i<len; i++)
				data[i] = cpu->cache[cache][(addr+i) &
				    cpu->cache_mask[cache]];
		} else {
			for (i=0; i<len; i++)
				cpu->cache[cache][(addr+i) &
				    cpu->cache_mask[cache]] = data[i];
		}
		return 1;
	} else {
		/*  Reload caches if necessary:  */

		/*  No!  Not when not emulating caches fully. (TODO?)  */
		cpu->cache_last_paddr[cache] = paddr;
	}
#endif

	return 0;
}


#define TRANSLATE_ADDRESS	translate_address_mmu3k
#define	V2P_MMU3K
#include "memory_v2p.c"
#undef TRANSLATE_ADDRESS
#undef V2P_MMU3K

#define TRANSLATE_ADDRESS	translate_address_mmu8k
#define	V2P_MMU8K
#include "memory_v2p.c"
#undef TRANSLATE_ADDRESS
#undef V2P_MMU8K

#define TRANSLATE_ADDRESS	translate_address_mmu10k
#define	V2P_MMU10K
#include "memory_v2p.c"
#undef TRANSLATE_ADDRESS
#undef V2P_MMU10K

/*  Almost generic  :-)  */
#define TRANSLATE_ADDRESS	translate_address_mmu4100
#define	V2P_MMU4100
#include "memory_v2p.c"
#undef TRANSLATE_ADDRESS
#undef V2P_MMU4100

#define TRANSLATE_ADDRESS	translate_address_generic
#include "memory_v2p.c"


/*
 *  memory_rw():
 *
 *  Read or write data from/to memory.
 *
 *	cpu		the cpu doing the read/write
 *	mem		the memory object to use
 *	vaddr		the virtual address
 *	data		a pointer to the data to be written to memory, or
 *			a placeholder for data when reading from memory
 *	len		the length of the 'data' buffer
 *	writeflag	set to MEM_READ or MEM_WRITE
 *	cache_flags	CACHE_{NONE,DATA,INSTRUCTION} | other flags
 *
 *  If the address indicates access to a memory mapped device, that device'
 *  read/write access function is called.
 *
 *  If instruction latency/delay support is enabled, then
 *  cpu->instruction_delay is increased by the number of instruction to
 *  delay execution.
 *
 *  This function should not be called with cpu == NULL.
 *
 *  Returns one of the following:
 *	MEMORY_ACCESS_FAILED
 *	MEMORY_ACCESS_OK
 *
 *  (MEMORY_ACCESS_FAILED is 0.)
 */
int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags)
{
	uint64_t paddr;
	int cache, no_exceptions, ok = 1, offset;
	unsigned char *memblock;
#ifdef BINTRANS
	int bintrans_cached = cpu->machine->bintrans_enable;
#endif
	no_exceptions = cache_flags & NO_EXCEPTIONS;
	cache = cache_flags & CACHE_FLAGS_MASK;


#ifdef BINTRANS
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->pc_bintrans_host_4kpage = NULL;
			cpu->pc_bintrans_paddr_valid = 0;
		}
	}
#endif

#ifdef ENABLE_USERLAND
	if (cpu->machine->userland_emul) {
		paddr = vaddr & 0x7fffffff;
		goto have_paddr;
	}
#endif

	/*
	 *  For instruction fetch, are we on the same page as the last
	 *  instruction we fetched?
	 *
	 *  NOTE: There's no need to check this stuff here if this address
	 *  is known to be in host ram, as it's done at instruction fetch
	 *  time in cpu.c!  Only check if _host_4k_page == NULL.
	 */
	if (cache == CACHE_INSTRUCTION &&
	    cpu->pc_last_host_4k_page == NULL &&
	    (vaddr & ~0xfff) == cpu->pc_last_virtual_page) {
		paddr = cpu->pc_last_physical_page | (vaddr & 0xfff);
		goto have_paddr;
	}

	if (cache_flags & PHYSICAL) {
		paddr = vaddr;
	} else {
		ok = cpu->translate_address(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) + (no_exceptions? FLAG_NOEXCEPTIONS : 0)
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));
		/*  If the translation caused an exception, or was invalid in some way,
			we simply return without doing the memory access:  */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}


	/*
	 *  If correct cache emulation is enabled, and we need to simluate
	 *  cache misses even from the instruction cache, we can't run directly
	 *  from a host page. :-/
	 */
#if defined(ENABLE_CACHE_EMULATION) && defined(ENABLE_INSTRUCTION_DELAYS)
#else
	if (cache == CACHE_INSTRUCTION) {
		cpu->pc_last_virtual_page = vaddr & ~0xfff;
		cpu->pc_last_physical_page = paddr & ~0xfff;
		cpu->pc_last_host_4k_page = NULL;

		/*  _last_host_4k_page will be set to 1 further down,
		    if the page is actually in host ram  */
	}
#endif


have_paddr:

	/*  TODO: How about bintrans vs cache emulation?  */
#ifdef BINTRANS
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->pc_bintrans_paddr_valid = 1;
			cpu->pc_bintrans_paddr = paddr;
		}
	}
#endif


	if (!(cache_flags & PHYSICAL))			/*  <-- hopefully this doesn't break anything (*)  */
		if (no_exceptions)
			goto no_exception_access;

/*  (*) = I need to access RAM devices easily without hardcoding stuff 
into the devices  */


	/*
	 *  Memory mapped device?
	 *
	 *  TODO: this is utterly slow.
	 *  TODO2: if paddr<base, but len enough, then we should write
	 *  to a device to
	 */
	if (paddr >= mem->mmap_dev_minaddr && paddr < mem->mmap_dev_maxaddr) {
#ifdef BINTRANS
		uint64_t orig_paddr = paddr;
#endif
		int i, start, res;
		i = start = mem->last_accessed_device;

		/*  Scan through all devices:  */
		do {
			if (paddr >= mem->dev_baseaddr[i] &&
			    paddr < mem->dev_baseaddr[i] + mem->dev_length[i]) {
				/*  Found a device, let's access it:  */
				mem->last_accessed_device = i;

				paddr -= mem->dev_baseaddr[i];
				if (paddr + len > mem->dev_length[i])
					len = mem->dev_length[i] - paddr;

#ifdef BINTRANS
				if (bintrans_cached && mem->dev_flags[i] & MEM_BINTRANS_OK) {
					int wf = writeflag == MEM_WRITE? 1 : 0;

					if (writeflag) {
						if (paddr < mem->dev_bintrans_write_low[i])
						    mem->dev_bintrans_write_low[i] = paddr & ~0xfff;
						if (paddr > mem->dev_bintrans_write_high[i])
						    mem->dev_bintrans_write_high[i] = paddr | 0xfff;
					}

					if (!(mem->dev_flags[i] & MEM_BINTRANS_WRITE_OK))
						wf = 0;

					update_translation_table(cpu, vaddr & ~0xfff,
					    mem->dev_bintrans_data[i] + (paddr & ~0xfff),
					    wf, orig_paddr & ~0xfff);
				}
#endif

				res = mem->dev_f[i](cpu, mem, paddr, data, len,
				    writeflag, mem->dev_extra[i]);

#ifdef ENABLE_INSTRUCTION_DELAYS
				if (res == 0)
					res = -1;

				cpu->instruction_delay +=
				    ( (abs(res) - 1) *
				     cpu->cpu_type.instrs_per_cycle );
#endif
				/*
				 *  If accessing the memory mapped device
				 *  failed, then return with a DBE exception.
				 */
				if (res <= 0) {
					debug("%s device '%s' addr %08lx failed\n",
					    writeflag? "writing to" : "reading from",
					    mem->dev_name[i], (long)paddr);

					mips_cpu_exception(cpu, EXCEPTION_DBE, 0,
					    vaddr, 0, 0, 0, 0);
					return MEMORY_ACCESS_FAILED;
				}

				goto do_return_ok;
			}

			i ++;
			if (i == mem->n_mmapped_devices)
				i = 0;
		} while (i != start);
	}


	/*
	 *  Data and instruction cache emulation:
	 */

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		/*  if not uncached addess  (TODO: generalize this)  */
		if (!(cache_flags & PHYSICAL) && cache != CACHE_NONE &&
		    !((vaddr & 0xffffffffULL) >= 0xa0000000ULL &&
		      (vaddr & 0xffffffffULL) <= 0xbfffffffULL)) {
			if (memory_cache_R3000(cpu, cache, paddr,
			    writeflag, len, data))
				goto do_return_ok;
		}
		break;
#if 0
/*  Remove this, it doesn't work anyway  */
	case MMU10K:
		/*  other cpus:  */
		/*
		 *  SUPER-UGLY HACK for SGI-IP32 PROM, R10000:
		 *  K0 bits == 0x3 means uncached...
		 *
		 *  It seems that during bootup, the SGI-IP32 prom
		 *  stores a return pointers a 0x80000f10, then tests
		 *  memory by writing bit patterns to 0xa0000xxx, and
		 *  then when it's done, reads back the return pointer
		 *  from 0x80000f10.
		 *
		 *  I need to find the correct way to disconnect the
		 *  cache from the main memory for R10000.  (TODO !!!)
		 */
/*		if ((cpu->coproc[0]->reg[COP0_CONFIG] & 7) == 3) {  */
/*
		if (cache == CACHE_DATA &&
		    cpu->r10k_cache_disable_TODO) {
			paddr &= ((512*1024)-1);
			paddr += 512*1024;
		}
*/
		break;
#endif
	default:
		/*  R4000 etc  */
		/*  TODO  */
		;
	}


	/*
	 *  Outside of physical RAM?  (For userland emulation we're using
	 *  the host's virtual memory and don't care about memory sizes,
	 *  so this doesn't apply.)
	 */
	if (paddr >= mem->physical_max
#ifdef ENABLE_USERLAND
	    && !cpu->machine->userland_emul
#endif
	    ) {
		if ((paddr & 0xffff000000ULL) == 0x1f000000) {
			/*  Ok, this is PROM stuff  */
		} else if ((paddr & 0xfffff00000ULL) == 0x1ff00000) {
			/*  Sprite reads from this area of memory...  */
			/*  TODO: is this still correct?  */
			if (writeflag == MEM_READ)
				memset(data, 0, len);
			goto do_return_ok;
		} else {
			if (paddr >= mem->physical_max + 0 * 1024) {
				char *symbol;
				uint64_t offset;

				if (!quiet_mode) {
					fatal("[ memory_rw(): writeflag=%i ", writeflag);
					if (writeflag) {
						unsigned int i;
						debug("data={", writeflag);
						if (len > 16) {
							int start2 = len-16;
							for (i=0; i<16; i++)
								debug("%s%02x", i?",":"", data[i]);
							debug(" .. ");
							if (start2 < 16)
								start2 = 16;
							for (i=start2; i<len; i++)
								debug("%s%02x", i?",":"", data[i]);
						} else
							for (i=0; i<len; i++)
								debug("%s%02x", i?",":"", data[i]);
						debug("}");
					}
					symbol = get_symbol_name(
					    &cpu->machine->symbol_context,
					    cpu->pc_last, &offset);
					fatal(" paddr=%llx >= physical_max pc=0x%08llx <%s> ]\n",
					    (long long)paddr, (long long)cpu->pc_last, symbol? symbol : "no symbol");
				}

				if (cpu->machine->single_step_on_bad_addr) {
					fatal("[ unimplemented access to "
					    "0x%016llx, pc = 0x%016llx ]\n",
					    (long long)paddr,
					    (long long)cpu->pc);
					single_step = 1;
				}
			}

			if (writeflag == MEM_READ) {
				/*  Return all zeroes? (Or 0xff? TODO)  */
				memset(data, 0, len);

				/*
				 *  For real data/instruction accesses, cause
				 *  an exceptions on an illegal read:
				 */
				if (cache != CACHE_NONE &&
				    cpu->machine->dbe_on_nonexistant_memaccess) {
					if (paddr >= mem->physical_max &&
					    paddr < mem->physical_max+1048576)
						mips_cpu_exception(cpu,
						    EXCEPTION_DBE, 0, vaddr, 0,
						    0, 0, 0);
				}
			}

			/*  Hm? Shouldn't there be a DBE exception for
			    invalid writes as well?  TODO  */

			goto do_return_ok;
		}
	}


no_exception_access:

	/*
	 *  Uncached access:
	 */
	memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
	if (memblock == NULL) {
		if (writeflag == MEM_READ)
			memset(data, 0, len);
		goto do_return_ok;
	}

	offset = paddr & ((1 << BITS_PER_MEMBLOCK) - 1);

#ifdef BINTRANS
	if (bintrans_cached)
		update_translation_table(cpu, vaddr & ~0xfff,
		    memblock + (offset & ~0xfff),
#if 0
		    cache == CACHE_INSTRUCTION?
			(writeflag == MEM_WRITE? 1 : 0)
			: ok - 1,
#else
		    writeflag == MEM_WRITE? 1 : 0,
#endif
		    paddr & ~0xfff);
#endif

	if (writeflag == MEM_WRITE) {
		if (len == sizeof(uint32_t) && (offset & 3)==0)
			*(uint32_t *)(memblock + offset) = *(uint32_t *)data;
		else if (len == sizeof(uint8_t))
			*(uint8_t *)(memblock + offset) = *(uint8_t *)data;
		else
			memcpy(memblock + offset, data, len);
	} else {
		if (len == sizeof(uint32_t) && (offset & 3)==0)
			*(uint32_t *)data = *(uint32_t *)(memblock + offset);
		else if (len == sizeof(uint8_t))
			*(uint8_t *)data = *(uint8_t *)(memblock + offset);
		else
			memcpy(data, memblock + offset, len);

		if (cache == CACHE_INSTRUCTION) {
			cpu->pc_last_host_4k_page = memblock
			    + (offset & ~0xfff);
#ifdef BINTRANS
			if (bintrans_cached) {
				cpu->pc_bintrans_host_4kpage =
				    cpu->pc_last_host_4k_page;
			}
#endif
		}
	}


do_return_ok:
	return MEMORY_ACCESS_OK;
}


/*
 *  memory_device_bintrans_access():
 *
 *  Get the lowest and highest bintrans access since last time.
 */
void memory_device_bintrans_access(struct cpu *cpu, struct memory *mem,
	void *extra, uint64_t *low, uint64_t *high)
{
#ifdef BINTRANS
	int i, j;
	size_t s;
	int need_inval = 0;

	/*  TODO: This is O(n), so it might be good to rewrite it some day.
	    For now, it will be enough, as long as this function is not
	    called too often.  */

	for (i=0; i<mem->n_mmapped_devices; i++) {
		if (mem->dev_extra[i] == extra &&
		    mem->dev_bintrans_data[i] != NULL) {
			if (mem->dev_bintrans_write_low[i] != (uint64_t) -1)
				need_inval = 1;
			if (low != NULL)
				*low = mem->dev_bintrans_write_low[i];
			mem->dev_bintrans_write_low[i] = (uint64_t) -1;

			if (high != NULL)
				*high = mem->dev_bintrans_write_high[i];
			mem->dev_bintrans_write_high[i] = 0;

/*			if (!need_inval)
				return;
*/
			/*  Invalidate any pages of this device that might
			    be in the bintrans load/store cache, by marking
			    the pages read-only.  */

			/*  TODO: This only works for R3000-style physical addresses!  */
			for (s=0; s<mem->dev_length[i]; s+=4096) {
#if 1
				invalidate_translation_caches_paddr(cpu,
				    mem->dev_baseaddr[i] + s);
#else
				update_translation_table(cpu,
				    mem->dev_baseaddr[i] + s + 0xffffffff80000000ULL,
				    mem->dev_bintrans_data[i] + s, -1, mem->dev_baseaddr[i] + s);
				update_translation_table(cpu,
				    mem->dev_baseaddr[i] + s + 0xffffffffa0000000ULL,
				    mem->dev_bintrans_data[i] + s, -1, mem->dev_baseaddr[i] + s);
#endif
			}

			/*  ... and invalidate the "fast_vaddr_to_hostaddr"
			    cache entries that contain pointers to this
			    device:  (NOTE: Device i, cache entry j)  */
			for (j=0; j<N_BINTRANS_VADDR_TO_HOST; j++) {
				if (cpu->bintrans_data_hostpage[j] >=
				    mem->dev_bintrans_data[i] &&
				    cpu->bintrans_data_hostpage[j] <
				    mem->dev_bintrans_data[i] +
				    mem->dev_length[i])
					cpu->bintrans_data_hostpage[j] = NULL;
			}

			return;
		}
	}
#endif
}


/*
 *  memory_device_register_statefunction():
 *
 *  TODO: Hm. This is semi-ugly. Should probably be rewritten/redesigned
 *  some day.
 */
void memory_device_register_statefunction(
	struct memory *mem, void *extra,
	int (*dev_f_state)(struct cpu *,
	    struct memory *, void *extra, int wf, int nr,
	    int *type, char **namep, void **data, size_t *len))
{
	int i;

	for (i=0; i<mem->n_mmapped_devices; i++)
		if (mem->dev_extra[i] == extra) {
			mem->dev_f_state[i] = dev_f_state;
			return;
		}

	printf("memory_device_register_statefunction(): couldn't find the device\n");
	exit(1);
}


/*
 *  memory_device_register():
 *
 *  Register a (memory mapped) device by adding it to the dev_* fields of a
 *  memory struct.
 */
void memory_device_register(struct memory *mem, const char *device_name,
	uint64_t baseaddr, uint64_t len,
	int (*f)(struct cpu *,struct memory *,uint64_t,unsigned char *,
		size_t,int,void *),
	void *extra, int flags, unsigned char *bintrans_data)
{
	if (mem->n_mmapped_devices >= MAX_DEVICES) {
		fprintf(stderr, "memory_device_register(): too many "
		    "devices registered, cannot register '%s'\n", device_name);
		exit(1);
	}

	/*  (40 bits of physical address is displayed)  */
	debug("device %2i at 0x%010llx: %s",
	    mem->n_mmapped_devices, (long long)baseaddr, device_name);

#ifdef BINTRANS
	if (flags & (MEM_BINTRANS_OK | MEM_BINTRANS_WRITE_OK)) {
		debug(" (bintrans %s)",
		    (flags & MEM_BINTRANS_WRITE_OK)? "R/W" : "R");
	}
#endif
	debug("\n");

	mem->dev_name[mem->n_mmapped_devices] = device_name;
	mem->dev_baseaddr[mem->n_mmapped_devices] = baseaddr;
	mem->dev_length[mem->n_mmapped_devices] = len;
	mem->dev_flags[mem->n_mmapped_devices] = flags;
	mem->dev_bintrans_data[mem->n_mmapped_devices] = bintrans_data;

	if ((size_t)bintrans_data & 1) {
		fprintf(stderr, "memory_device_register():"
		    " bintrans_data not aligned correctly\n");
		exit(1);
	}

#ifdef BINTRANS
	mem->dev_bintrans_write_low[mem->n_mmapped_devices] = (uint64_t)-1;
	mem->dev_bintrans_write_high[mem->n_mmapped_devices] = 0;
#endif
	mem->dev_f[mem->n_mmapped_devices] = f;
	mem->dev_extra[mem->n_mmapped_devices] = extra;
	mem->n_mmapped_devices++;

	if (baseaddr < mem->mmap_dev_minaddr)
		mem->mmap_dev_minaddr = baseaddr;
	if (baseaddr + len > mem->mmap_dev_maxaddr)
		mem->mmap_dev_maxaddr = baseaddr + len;
}


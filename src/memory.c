/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: memory.c,v 1.53 2004-07-04 03:28:47 debug Exp $
 *
 *  Functions for handling the memory of an emulated machine.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "memory.h"
#include "misc.h"

extern int emulation_type;
extern int physical_ram_in_mb;
extern int machine;
extern int instruction_trace;
extern int register_dump;
extern int trace_on_bad_address;
extern int userland_emul;
extern int tlb_dump;
extern int quiet_mode;
extern int use_x11;


/*
 *  memory_readmax64():
 *
 *  Read at most 64 bits of data from a buffer.  Length is given by
 *  len, and the byte order by cpu->byte_order.
 *
 *  TODO:  Maybe this shouldn't be in memory.c.  It's a kind of 'misc'
 *  helper function.
 */
uint64_t memory_readmax64(struct cpu *cpu, unsigned char *buf, int len)
{
	int i;
	uint64_t x = 0;

#if 0
	if (cpu == NULL) {
		fatal("memory_readmax64(): cpu = NULL\n");
		exit(1);
	}

	if (len < 1 || len > 8) {
		fatal("memory_readmax64(): len = %i\n", len);
		exit(1);
	}
#endif

	/*  Switch byte order for incoming data, if neccessary:  */
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
 *  TODO:  Maybe this shouldn't be in memory.c.  It's a kind of 'misc'
 *  helper function.
 */
void memory_writemax64(struct cpu *cpu, unsigned char *buf, int len,
	uint64_t data)
{
	int i;

#if 0
	if (cpu == NULL) {
		fatal("memory_readmax64(): cpu = NULL\n");
		exit(1);
	}

	if (len < 1 || len > 8) {
		fatal("memory_readmax64(): len = %i\n", len);
		exit(1);
	}
#endif

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		for (i=0; i<len; i++)
			buf[i] = (data >> (i*8)) & 255;
	} else {
		for (i=0; i<len; i++)
			buf[len - 1 - i] = (data >> (i*8)) & 255;
	}
}


/*
 *  memory_new():
 *
 *  This function creates a new memory object. An emulated machine needs one
 *  of these.
 */
struct memory *memory_new(int bits_per_pagetable, int bits_per_memblock,
	uint64_t physical_max, int max_bits)
{
	struct memory *mem;
	int n, ok;

	mem = malloc(sizeof(struct memory));
	if (mem == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	memset(mem, 0, sizeof(struct memory));

	/*
	 *  Check bits_per_pagetable and bits_per_memblock for sanity:
	 *  bits_per_pagetable * n + bits_per_memblock = 64, for some
	 *  integer n.
	 */
	ok = 0;
	for (n=64; n>0; n--)
		if (bits_per_pagetable * n + bits_per_memblock == max_bits)
			ok = 1;

	if (!ok) {
		fprintf(stderr, "memory_new(): bits_per_pagetable and bits_per_memblock mismatch\n");
		exit(1);
	}

	mem->bits_per_pagetable = bits_per_pagetable;
	mem->entries_per_pagetable = 1 << bits_per_pagetable;
	mem->bits_per_memblock = bits_per_memblock;
	mem->memblock_size = 1 << bits_per_memblock;
	mem->max_bits = max_bits;

	mem->physical_max = physical_max;

	mem->first_pagetable = malloc(mem->entries_per_pagetable
	    * sizeof(void *));
	if (mem->first_pagetable == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(mem->first_pagetable, 0, mem->entries_per_pagetable
	    * sizeof(void *));

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


#define	FLAG_WRITEFLAG		1
#define	FLAG_NOEXCEPTIONS	2
#define	FLAG_INSTR		4


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
	unsigned char *memblock;
	void **table;
	int entry, shrcount, mask, bits_per_memblock, bits_per_pagetable;

	bits_per_memblock  = mem->bits_per_memblock;
	bits_per_pagetable = mem->bits_per_pagetable;
	memblock = NULL;
	table = mem->first_pagetable;
	mask = mem->entries_per_pagetable - 1;
	shrcount = mem->max_bits - bits_per_pagetable;

	/*
	 *  Step through the pagetables until we find the correct memory
	 *  block:
	 */
	while (shrcount >= bits_per_memblock) {
		/*  printf("addr = %016llx\n", paddr);  */
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

			/*  Allocate a pagetable, OR a memblock:  */
			if (shrcount == bits_per_memblock)
				alloclen = mem->memblock_size;
			else
				alloclen = mem->entries_per_pagetable
				    * sizeof(void *);

			/*  printf("  allocating for entry %i, len=%i\n",
			    entry, alloclen);  */

			table[entry] = malloc(alloclen);
			if (table[entry] == NULL) {
				fatal("out of memory\n");
				exit(1);
			}
			memset(table[entry], 0, alloclen);
		}

		if (shrcount == bits_per_memblock)
			memblock = (unsigned char *) table[entry];
		else
			table = (void **) table[entry];

		shrcount -= bits_per_pagetable;
	}

	return memblock;
}


/*
 *  memory_cache_R3000():
 *
 *  R3000 specific cache handling.
 *
 *  Return value is 1 if a jump to do_return_ok is supposed to happen directly
 *  after this routine is finished, 0 otherwise.
 */
int memory_cache_R3000(struct cpu *cpu, int cache, uint64_t paddr,
	int writeflag, size_t len, unsigned char *data)
{
#ifdef ENABLE_CACHE_EMULATION
	unsigned char *memblock;
	struct memory *mem = cpu->mem;
	int offset;
#endif
	unsigned int i;
	int cache_isolated = 0, addr, hit, which_cache = cache;


	if (cpu->coproc[0]->reg[COP0_STATUS] & MIPS1_SWAP_CACHES)
		which_cache ^= 1;

	/*  Is this a cache hit or miss?  */
	hit = (cpu->cache_last_paddr[which_cache]
		& ~cpu->cache_mask[which_cache])
	    == (paddr & ~(cpu->cache_mask[which_cache]));

#ifdef ENABLE_INSTRUCTION_DELAYS
	if (!hit)
		cpu->instruction_delay += cpu->cpu_type.instrs_per_cycle
		    * cpu->cache_miss_penalty[cache];
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

#ifdef ENABLE_CACHE_EMULATION
	/*
	 *  If there was a miss and the cache is not isolated, reload cache
	 *  contents from main memory.
	 *
	 *  Then access the cache.
	 */
	/*
	fatal("L1 CACHE isolated=%i hit=%i write=%i cache=%i"
	    " vaddr=%016llx paddr=%016llx => addr in"
	    " cache = 0x%lx\n", cache_isolated, hit, writeflag,
	    which_cache, (long long)vaddr, (long long)paddr,
	    addr);
	*/

	if (!hit && !cache_isolated) {
		unsigned char *dst, *src;
		uint64_t old_cached_paddr =
		    cpu->cache_last_paddr[which_cache];

		/*  Flush the cache to main memory:  */
		if (old_cached_paddr != IMPOSSIBLE_PADDR) {
			memblock = memory_paddr_to_hostaddr(
			    mem, old_cached_paddr, MEM_WRITE);
			offset = old_cached_paddr
			    & (mem->memblock_size - 1)
			    & ~cpu->cache_mask[which_cache];

			src = cpu->cache[which_cache];
			dst = memblock + (offset &
			    ~cpu->cache_mask[which_cache]);

			if (memblock == NULL) {
				fatal("BUG in memory.c! Hm.\n");
			} else {
				memcpy(dst, src, cpu->cache_size[which_cache]);
			}
			/*  offset is the offset within
			 *  the memblock:
			 *  printf("read: offset = 0x%x\n", offset);
			 */
		}

		/*  Copy from main memory into the cache:  */
		memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
		offset = paddr & (mem->memblock_size - 1)
		    & ~cpu->cache_mask[which_cache];
		/*  offset is offset within the memblock:
		 *  printf("write: offset = 0x%x\n", offset);
		 */
		dst = cpu->cache[which_cache];

		if (memblock == NULL) {
			if (writeflag == MEM_READ)
			memset(dst, 0, cpu->cache_size[which_cache]);
		} else {
			src = memblock + (offset &
			    ~cpu->cache_mask[which_cache]);
			memcpy(dst, src, cpu->cache_size[which_cache]);
		}

		cpu->cache_last_paddr[which_cache] = paddr;
	}

	if (writeflag==MEM_READ) {
		for (i=0; i<len; i++)
			data[i] = cpu->cache[which_cache][(addr+i) &
			    cpu->cache_mask[which_cache]];
	} else {
		for (i=0; i<len; i++)
			cpu->cache[which_cache][(addr+i) &
			    cpu->cache_mask[which_cache]] = data[i];
	}

	/*  Run instructions directly from the cache:  */
	if (cache == CACHE_INSTRUCTION) {
		cpu->pc_last_was_in_host_ram = 1;
		cpu->pc_last_host_4k_page =
		    cpu->cache[which_cache] + (addr & ~0xfff);
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
		/*  Reload caches if neccessary:  */

		/*  No!  Not when not emulating caches fully. (TODO?)  */
		cpu->cache_last_paddr[cache] = paddr;
	}
#endif

	return 0;
}


/*
 *  translate_address():
 *
 *  Don't call this function is userland_emul is non-zero, or cpu is NULL.
 *
 *  TODO:  This is long and ugly.
 *  Also, it needs to be rewritten for each CPU (?)
 *
 *  TODO:  vpn2 is a bad name for R2K/R3K, as it is the actual framenumber
 */
static int translate_address(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags)
{
	int writeflag = flags & FLAG_WRITEFLAG? MEM_WRITE : MEM_READ;
	int no_exceptions = flags & FLAG_NOEXCEPTIONS;
	int instr = flags & FLAG_INSTR;
	int ksu, use_tlb, status, i;
	int x_64;	/*  non-zero for 64-bit address space accesses  */
	uint64_t vaddr_vpn2=0, vaddr_asid=0;
	int pageshift, exccode, tlb_refill, mmumodel3k, n_tlbs;
	struct coproc *cp0;

#ifdef USE_TINY_CACHE
	/*
	 *  Check the tiny translation cache first:
	 *
	 *  Only userland addresses are checked, because other addresses
	 *  are probably better of being statically translated, or through
	 *  the TLB.  (Note: When running with 64-bit addresses, this
	 *  will still produce the correct result. At worst, we check the
	 *  cache in vain, but the result should still be correct.)
	 */
	if ((vaddr & 0xc0000000ULL) != 0x80000000ULL) {
		int i, wf = 1 + (writeflag == MEM_WRITE);
		uint64_t vaddr_shift_12 = vaddr >> 12;

		if (instr) {
			/*  Code:  */
			for (i=0; i<N_TRANSLATION_CACHE_INSTR; i++) {
				if (cpu->translation_cache_instr[i].wf >= wf &&
				    vaddr_shift_12 == (cpu->translation_cache_instr[i].vaddr_pfn)) {
					*return_addr = cpu->translation_cache_instr[i].paddr | (vaddr & 0xfff);
					return 1;
				}
			}
		} else {
			/*  Data:  */
			for (i=0; i<N_TRANSLATION_CACHE_DATA; i++) {
				if (cpu->translation_cache_data[i].wf >= wf &&
				    vaddr_shift_12 == (cpu->translation_cache_data[i].vaddr_pfn)) {
					*return_addr = cpu->translation_cache_data[i].paddr | (vaddr & 0xfff);
					return 1;
				}
			}
		}
	}
#endif

	/*  Cached values:  */
	mmumodel3k = cpu->cpu_type.mmu_model == MMU3K;
	cp0 = cpu->coproc[0];
	status = cp0->reg[COP0_STATUS];
	n_tlbs = cpu->cpu_type.nr_of_tlb_entries;

	/*
	 *  R4000 Address Translation:
	 *
	 *  An address may be in one of the kernel segments, that
	 *  are directly mapped, or the address can go through the
	 *  TLBs to be turned into a physical address.
	 *
	 *  KSU: EXL: ERL: X:  Name:   Range:
	 *  ---- ---- ---- --  -----   ------
	 *
	 *   10   0    0    0  useg    0 - 0x7fffffff    (2GB)  (via TLB)
	 *   10   0    0    1  xuseg   0 - 0xffffffffff  (1TB)  (via TLB)
	 *
	 *   01   0    0    0  suseg   0          - 0x7fffffff                  (2GB)  (via TLB)
	 *   01   0    0    0  ssseg   0xc0000000 - 0xdfffffff                  (0.5 GB)  (via TLB)
	 *   01   0    0    1  xsuseg  0 - 0xffffffffff                         (1TB)  (via TLB)
	 *   01   0    0    1  xsseg   0x4000000000000000 - 0x400000ffffffffff  (1TB)  (via TLB)
	 *   01   0    0    1  csseg   0xffffffffc0000000 - 0xffffffffdfffffff  (0.5TB)  (via TLB)
	 *
	 *   00   x    x    0  kuseg   0 - 0x7fffffff  (2GB)  (via TLB)  (*)
	 *   00   x    x    0  kseg0   0x80000000 - 0x9fffffff (0.5GB)  unmapped, cached
	 *   00   x    x    0  kseg1   0xa0000000 - 0xbfffffff (0.5GB)  unmapped, uncached
	 *   00   x    x    0  ksseg   0xc0000000 - 0xdfffffff (0.5GB)  (via TLB)
	 *   00   x    x    0  kseg3   0xe0000000 - 0xffffffff (0.5GB)  (via TLB)
	 *   00   x    x    1  xksuseg 0 - 0xffffffffff (1TB) (via TLB) (*)
	 *   00   x    x    1  xksseg  0x4000000000000000 - 0x400000ffffffffff  (1TB)  (via TLB)
	 *   00   x    x    1  xkphys  0x8000000000000000 - 0xbfffffffffffffff  todo
	 *   00   x    x    1  xkseg   0xc000000000000000 - 0xc00000ff7fffffff  todo
	 *   00   x    x    1  ckseg0  0xffffffff80000000 - 0xffffffff9fffffff  like kseg0
	 *   00   x    x    1  ckseg1  0xffffffffa0000000 - 0xffffffffbfffffff  like kseg1
	 *   00   x    x    1  cksseg  0xffffffffc0000000 - 0xffffffffdfffffff  like ksseg
	 *   00   x    x    1  ckseg3  0xffffffffe0000000 - 0xffffffffffffffff  like kseg2
	 *
	 *  (*) = if ERL=1 then kuseg is not via TLB, but unmapped, uncached physical memory.
	 *
	 *  (KSU==0 or EXL=1 or ERL=1 is enough to use k*seg*.)
	 *
	 *  An invalid address causes an Address Error.
	 *
	 *  See chapter 4, page 96, in the R4000 manual for more info!
	 */

	if (mmumodel3k) {
		if (status & MIPS1_SR_KU_CUR)
			ksu = KSU_USER;
		else
			ksu = KSU_KERNEL;

		pageshift = 12;
		x_64 = 0;

		/*  These are needed later:  */
		vaddr_asid = (cp0->reg[COP0_ENTRYHI] & R2K3K_ENTRYHI_ASID_MASK)
		    >> R2K3K_ENTRYHI_ASID_SHIFT;
		vaddr_vpn2 = (vaddr & R2K3K_ENTRYHI_VPN_MASK) >> 12;
	} else {
		/*
		 *  R4000 and others:
		 *
		 *  kx,sx,ux = 0 for 32-bit addressing,
		 *  1 for 64-bit addressing. 
		 */
		ksu = (status & STATUS_KSU_MASK) >> STATUS_KSU_SHIFT;
		if (status & (STATUS_EXL | STATUS_ERL))
			ksu = KSU_KERNEL << STATUS_KSU_SHIFT;

		/*  Assume KSU_USER.  */
		x_64 = status & STATUS_UX;

		if (ksu == KSU_KERNEL)
			x_64 = status & STATUS_KX;
		else if (ksu == KSU_SUPERVISOR)
			x_64 = status & STATUS_SX;

		/*
		 *  Special case hacks, mostly for SGI machines:
		 *
		 *  0x9000000080000000 = disable L2 cache (?)
		 *  TODO:  Make this correct.
		 */
		switch (vaddr >> 60) {
		case 8:
		case 9:		/*  0x9000...  */
			/*
			 *  On IP30, addresses such as 0x900000001f600050 are used,
			 *  but also things like 0x90000000a0000000.  (TODO)
			 */
			*return_addr = vaddr & (((uint64_t)1 << 44) - 1);
			return 1;
		case 0xa:		/*  like 0xa8...  */
			*return_addr = vaddr;
			return 1;
		/*
		 *  TODO:  SGI-IP27 and others, when running Irix, seem to
		 *  use these kernel-virtual-addresses as if they were
		 *  physical addresses.  Maybe a static mapping in the
		 *  tlb, say entry #0, would be a good solution?
		 */
		case 0xc:
			if (emulation_type == EMULTYPE_SGI && machine >= 25) {
				*return_addr = vaddr &
				    (((uint64_t)1 << 44) - 1);
				return 1;
			}
			break;
		default:
			;
		}

		/*  This suppresses compiler warning:  */
		pageshift = 12;

		/*  This is needed later:  */
		vaddr_asid = cp0->reg[COP0_ENTRYHI] & ENTRYHI_ASID;
		/*  vpn2 depends on pagemask, which is not fixed on R4000  */
	}

	if (vaddr <= 0x7fffffff)
		use_tlb = 1;
	else {
		/*  Sign-extend vaddr, if neccessary:  */
		if ((vaddr >> 32) == 0 && vaddr & (uint32_t)0x80000000ULL)
			vaddr |= 0xffffffff00000000ULL;

		if (ksu == KSU_KERNEL) {
			/*  kseg0, kseg1:  */
			if (vaddr >= (uint64_t)0xffffffff80000000ULL &&
			    vaddr <= (uint64_t)0xffffffffbfffffffULL) {
				*return_addr = vaddr & 0x1fffffff;
				return 1;
			}

			/*  TODO: supervisor stuff  */

			/*  other segments:  */
			use_tlb = 1;
		} else
			use_tlb = 0;
	}

	exccode = -1;
	tlb_refill = 1;

	if (tlb_dump && !no_exceptions) {
		int i;
		debug("{ vaddr=%016llx ==> ? }\n", (long long)vaddr);
		for (i=0; i<cpu->cpu_type.nr_of_tlb_entries; i++) {
			char *symbol;
			int offset;

			symbol = get_symbol_name(cpu->pc_last, &offset);
			/*  debug("pc = 0x%08llx <%s>\n", (long long)cpu->pc_last, symbol? symbol : "no symbol");  */
			debug("tlb entry %2i: mask=%016llx hi=%016llx lo1=%016llx lo0=%016llx\n",
				i, cp0->tlbs[i].mask, cp0->tlbs[i].hi, cp0->tlbs[i].lo1, cp0->tlbs[i].lo0);
		}
	}

	if (use_tlb) {
		int g_bit, v_bit, d_bit, pmask = 0xfff, odd = 0;
		uint64_t cached_hi, cached_lo0, cached_lo1;
		uint64_t entry_vpn2 = 0, entry_asid, pfn;

		for (i=0; i<n_tlbs; i++) {
			if (mmumodel3k) {
				/*  R3000 or similar:  */
				cached_hi = cp0->tlbs[i].hi;
				cached_lo0 = cp0->tlbs[i].lo0;

				entry_vpn2 = (cached_hi & R2K3K_ENTRYHI_VPN_MASK) >> R2K3K_ENTRYHI_VPN_SHIFT;
				entry_asid = (cached_hi & R2K3K_ENTRYHI_ASID_MASK) >> R2K3K_ENTRYHI_ASID_SHIFT;
				g_bit = cached_lo0 & R2K3K_ENTRYLO_G;

				pfn = (cached_lo0 & R2K3K_ENTRYLO_PFN_MASK) >> R2K3K_ENTRYLO_PFN_SHIFT;
				v_bit = cached_lo0 & R2K3K_ENTRYLO_V;
				d_bit = cached_lo0 & R2K3K_ENTRYLO_D;
			} else {
				/*  R4000 or similar:  */
				pmask = (cp0->tlbs[i].mask &
				    PAGEMASK_MASK) | 0x1fff;
				cached_hi = cp0->tlbs[i].hi;
				cached_lo0 = cp0->tlbs[i].lo0;
				cached_lo1 = cp0->tlbs[i].lo1;

				/*  Optimized for 4KB page size:  */
				if (pmask == 0x1fff) {
					pageshift = 12;
					entry_vpn2 = (cached_hi &
					    ENTRYHI_VPN2_MASK) >> 13;
					vaddr_vpn2 = (vaddr &
					    ENTRYHI_VPN2_MASK) >> 13;
					pmask = 0xfff;
					odd = vaddr & 0x1000;
				} else {
					/*  Non-standard page mask:  */
					switch (pmask) {
					case 0x0007fff:	pageshift = 14; break;
					case 0x001ffff:	pageshift = 16; break;
					case 0x007ffff:	pageshift = 18; break;
					case 0x01fffff:	pageshift = 20; break;
					case 0x07fffff:	pageshift = 22; break;
					case 0x1ffffff:	pageshift = 24; break;
					case 0x7ffffff:	pageshift = 26; break;
					default:
						fatal("pmask=%08x\n", i, pmask);
						exit(1);
					}

					entry_vpn2 = (cached_hi &
					    ENTRYHI_VPN2_MASK) >>
					    (pageshift + 1);
					vaddr_vpn2 = (vaddr &
					    ENTRYHI_VPN2_MASK) >>
					    (pageshift + 1);
					pmask >>= 1;
					odd = (vaddr >> pageshift) & 1;
				}

				/*  Assume even virtual page...  */
				pfn = (cached_lo0 & ENTRYLO_PFN_MASK) >> ENTRYLO_PFN_SHIFT;
				v_bit = cached_lo0 & ENTRYLO_V;
				d_bit = cached_lo0 & ENTRYLO_D;

				switch (cpu->cpu_type.mmu_model) {
				case MMU10K:
					entry_vpn2 = (cached_hi & ENTRYHI_VPN2_MASK_R10K) >> (pageshift + 1);
					vaddr_vpn2 = (vaddr & ENTRYHI_VPN2_MASK_R10K) >> (pageshift + 1);
					break;
				case MMU8K:
					/*
					 *  TODO:  I don't really know anything about the R8000.
					 *  http://futuretech.mirror.vuurwerk.net/i2sec7.html
					 *  says that it has a three-way associative TLB with
					 *  384 entries, 16KB page size, and some other things.
					 *
					 *  It feels like things like the valid bit (ala R4000)
					 *  and dirty bit are not implemented the same on R8000.
					 *
					 *  http://sgistuff.tastensuppe.de/documents/R8000_chipset.html
					 *  also has some info, but no details.
					 */
					v_bit = 1;	/*  Big TODO  */
					d_bit = 1;
				}

				entry_asid = cached_hi & ENTRYHI_ASID;
				g_bit = cached_hi & TLB_G;

				/*  ... reload pfn, v_bit, d_bit if
				    it was the odd virtual page:  */
				if (odd) {
					pfn = (cached_lo1 & ENTRYLO_PFN_MASK) >> ENTRYLO_PFN_SHIFT;
					v_bit = cached_lo1 & ENTRYLO_V;
					d_bit = cached_lo1 & ENTRYLO_D;
				}
			}

			/*  Is there a VPN and ASID match?  */
			if (entry_vpn2 == vaddr_vpn2 &&
			    (entry_asid == vaddr_asid || g_bit)) {
				/*  debug("OK MAP 1!!! { vaddr=%016llx ==> paddr %016llx v=%i d=%i asid=0x%02x }\n",
				    (long long)vaddr, (long long) *return_addr, v_bit?1:0, d_bit?1:0, vaddr_asid);  */
				if (v_bit) {
					if (d_bit || (!d_bit && writeflag==MEM_READ)) {
						uint64_t paddr;
						/*  debug("OK MAP 2!!! { w=%i vaddr=%016llx ==> d=%i v=%i paddr %016llx ",
						    writeflag, (long long)vaddr, d_bit?1:0, v_bit?1:0, (long long) *return_addr);
						    debug(", tlb entry %2i: mask=%016llx hi=%016llx lo0=%016llx lo1=%016llx\n",
							i, cp0->tlbs[i].mask, cp0->tlbs[i].hi, cp0->tlbs[i].lo0, cp0->tlbs[i].lo1);  */

						paddr = (pfn << pageshift) |
						    (vaddr & pmask);
#ifdef LAST_USED_TLB_EXPERIMENT
						cp0->tlbs[i].last_used = cp0->reg[COP0_COUNT];
#endif
						/*
						 *  Enter into the tiny trans-
						 *  lation cache (if enabled)
						 *  and return:
						 */
						insert_into_tiny_cache(cpu,
						    instr, writeflag,
						    vaddr, paddr);

						*return_addr = paddr;
						return 1;
					} else {
						/*  TLB modification exception  */
						tlb_refill = 0;
						exccode = EXCEPTION_MOD;
						goto exception;
					}
				} else {
					/*  TLB invalid exception  */
					tlb_refill = 0;
					goto exception;
				}
			}
		}
	}

	/*
	 *  We are here if for example userland code tried to access
	 *  kernel memory.
	 */

#if 0
	if (!use_tlb) {
		int i;

		quiet_mode = 0;
		instruction_trace = register_dump = 1;

		debug("not using tlb, but still no hit. TODO\n");

		debug("{ vaddr=%016llx ==> ? pc=%016llx }\n", (long long)vaddr, (long long)cpu->pc);

		for (i=0; i<cpu->cpu_type.nr_of_tlb_entries; i++) {
			debug("tlb entry %2i: mask=%016llx  hi=%016llx"
			      " lo1=%016llx lo0=%016llx\n",
				i,
				cp0->tlbs[i].mask,
				cp0->tlbs[i].hi,
				cp0->tlbs[i].lo1,
				cp0->tlbs[i].lo0);
		}
		exit(1);
	}
#endif


	/*  TLB refill  */

exception:
	if (no_exceptions)
		return 0;

	/*  TLB Load or Store exception:  */
	if (exccode == -1) {
		if (writeflag == MEM_WRITE)
			exccode = EXCEPTION_TLBS;
		else
			exccode = EXCEPTION_TLBL;
	}

	cpu_exception(cpu, exccode, tlb_refill, vaddr,
	    0, vaddr_vpn2, vaddr_asid, x_64);

	/*  Return failure:  */
	return 0;
}


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
 *  Returns one of the following:
 *	MEMORY_ACCESS_FAILED
 *	MEMORY_ACCESS_OK
 *	INSTR_BINTRANS		(TODO)
 *
 *  (MEMORY_ACCESS_FAILED is 0.)
 */
int memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags)
{
	uint64_t paddr, endaddr = vaddr + len - 1;
	int cache, no_exceptions, ok, offset;
	unsigned char *memblock;

	no_exceptions = cache_flags & NO_EXCEPTIONS;
	cache = cache_flags & CACHE_FLAGS_MASK;

	if (cpu == NULL) {
		paddr = vaddr & 0x1fffffff;
		goto have_paddr;
	}

	if (userland_emul) {
		paddr = vaddr & 0x7fffffff;
		goto have_paddr;
	}

	/*
	 *  For instruction fetch, are we on the same page as the last
	 *  instruction we fetched?
	 *
	 *  NOTE: There's no need to check this stuff here if
	 *  pc_last_was_in_host_ram is true, as it's done at instruction fetch
	 *  time in cpu.c!  Only check if _in_host_ram == 0.
	 *
	 *  NOTE 2:  cpu may be NULL here, but "hopefully" not if
	 *  cache == CACHE_INSTRUCTION.  (TODO)
	 */
	if (cache == CACHE_INSTRUCTION &&
	    !cpu->pc_last_was_in_host_ram &&
	    (vaddr & ~0xfff) == cpu->pc_last_virtual_page) {
		paddr = cpu->pc_last_physical_page | (vaddr & 0xfff);
		goto have_paddr;
	}


	if (cache_flags & PHYSICAL) {
		paddr = vaddr;
	} else {
		ok = translate_address(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) + (no_exceptions? FLAG_NOEXCEPTIONS : 0)
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));
		/*  If the translation caused an exception, or was invalid in some way,
			we simply return without doing the memory access:  */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}


	/*
	 *  Physical addresses of the form 0xa8........ and 0x90..........
	 *  don't actually have all 64 bit significant, only the lower part.
	 *
	 *  (TODO:  Is this SGI specific?)  (TODO 2:  Is 48 bits ok?)
	 *
	 *  It seems that some Ultrix code (or OSF/1) seems to use addresses
	 *  such as 0xbe000000 as if they were physical addresses. (It should
	 *  be 0x1e000000, so I just take the lowest bits here.)
	 */
	if (emulation_type == EMULTYPE_DEC)
		paddr &= 0x1fffffff;
	else
		paddr &= (((uint64_t)1<<(uint64_t)48) - 1);


	if (cache == CACHE_INSTRUCTION) {
		cpu->pc_last_virtual_page = vaddr & ~0xfff;
		cpu->pc_last_physical_page = paddr & ~0xfff;
		cpu->pc_last_was_in_host_ram = 0;

		/*  _last_was_in_host_ram will be set to 1 further down,
		    if the page is actually in host ram  */
	}


have_paddr:

	if (!(cache_flags & PHYSICAL))			/*  <-- hopefully this doesn't break anything (*)  */
		if (no_exceptions && cpu != NULL)
			goto no_exception_access;

/*  (*) = I need to access RAM devices easily without hardcoding stuff 
into the devices  */


	/*
	 *  Memory mapped device?
	 *
	 *  NOTE: cpu may be NULL.
	 *
	 *  TODO: this is utterly slow.
	 *  TODO2: if paddr<base, but len enough, then we should write
	 *  to a device to
	 */
	if (paddr >= mem->mmap_dev_minaddr && paddr < mem->mmap_dev_maxaddr) {
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

				res = mem->dev_f[i](cpu, mem, paddr, data, len,
				    writeflag, mem->dev_extra[i]);

#ifdef ENABLE_INSTRUCTION_DELAYS
				if (res == 0)
					res = -1;

				if (cpu != NULL)
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

					cpu_exception(cpu, EXCEPTION_DBE, 0,
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


	if (cpu == NULL)
		goto no_exception_access;


	/*  Accesses that cross memory blocks are bad:  */
	endaddr = paddr + len - 1;
	endaddr &= ~(mem->memblock_size - 1);
	if ( (paddr & ~(mem->memblock_size - 1)) != endaddr ) {
		debug("memory access crosses memory block? "
		    "paddr=%016llx len=%i\n", (long long)paddr, (int)len);
		return MEMORY_ACCESS_FAILED;
	}


	/*
	 *  Data and instruction cache emulation:
	 */
	/*  vaddr shouldn't be used below anyway,
	    except for in the next 'if' statement:  */
	if ((vaddr >> 32) == 0xffffffffULL)
		vaddr &= 0xffffffffULL;

	switch (cpu->cpu_type.mmu_model) {
	case MMU3K:
		/*  if not uncached addess  (TODO: generalize this)  */
		if (!(cache_flags & PHYSICAL) && cache != CACHE_NONE &&
		    !(vaddr >= 0xa0000000ULL && vaddr <= 0xbfffffffULL)) {
			if (memory_cache_R3000(cpu, cache, paddr,
			    writeflag, len, data))
				goto do_return_ok;
		}
		break;
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
		if (cache == CACHE_DATA &&
		    cpu->r10k_cache_disable_TODO) {
			paddr &= ((512*1024)-1);
			paddr += 512*1024;
		}
		break;
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
	if (paddr >= mem->physical_max && !userland_emul) {
		if ((paddr & 0xffffc00000ULL) == 0x1fc00000) {
			/*  Ok, this is PROM stuff  */
		} else {
			if (paddr >= mem->physical_max + 0 * 1024) {
				char *symbol;
				int offset;

				if (!quiet_mode) {
					debug("[ memory_rw(): writeflag=%i ", writeflag);
					if (writeflag) {
						unsigned int i;
						debug("data={", writeflag);
						for (i=0; i<len; i++)
							debug("%s%02x", i?",":"", data[i]);
						debug("}");
					}
					symbol = get_symbol_name(cpu->pc_last, &offset);
					debug(" paddr=%llx >= physical_max pc=0x%08llx <%s> ]\n",
					    (long long)paddr, (long long)cpu->pc_last, symbol? symbol : "no symbol");
				}

				if (trace_on_bad_address) {
					instruction_trace = register_dump = 1;
					quiet_mode = 0;
				}
			}

			if (writeflag == MEM_READ) {
				/*  Return all zeroes? (Or 0xff? TODO)  */
				memset(data, 0, len);

				/*
				 *  For real data/instruction accesses, cause
				 *  an exceptions on an illegal read:
				 */
				if (cache != CACHE_NONE) {
					if (paddr >= mem->physical_max &&
					    paddr < mem->physical_max+1048576)
						cpu_exception(cpu,
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

	offset = paddr & (mem->memblock_size - 1);

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
			cpu->pc_last_was_in_host_ram = 1;
			cpu->pc_last_host_4k_page = memblock
			    + (offset & ~0xfff);
		}
	}

do_return_ok:
	return MEMORY_ACCESS_OK;
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
	void *extra)
{
	if (mem->n_mmapped_devices >= MAX_DEVICES) {
		fprintf(stderr, "memory_device_register(): too many devices registered, cannot register '%s'\n", device_name);
		exit(1);
	}

	debug("adding device %i at 0x%08lx: %s\n",
	    mem->n_mmapped_devices, (long)baseaddr, device_name);

	mem->dev_name[mem->n_mmapped_devices] = device_name;
	mem->dev_baseaddr[mem->n_mmapped_devices] = baseaddr;
	mem->dev_length[mem->n_mmapped_devices] = len;
	mem->dev_f[mem->n_mmapped_devices] = f;
	mem->dev_extra[mem->n_mmapped_devices] = extra;
	mem->n_mmapped_devices++;

	if (baseaddr < mem->mmap_dev_minaddr)
		mem->mmap_dev_minaddr = baseaddr;
	if (baseaddr + len > mem->mmap_dev_maxaddr)
		mem->mmap_dev_maxaddr = baseaddr + len;
}



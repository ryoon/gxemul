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
 *  $Id: memory_rw.c,v 1.7 2005-02-18 08:07:59 debug Exp $
 *
 *  Generic memory_rw(), with special hacks for specific CPU families.
 *
 *  Example for inclusion from memory_mips.c:
 *
 *	MEMORY_RW should be mips_memory_rw
 *	MEM_MIPS should be defined
 */


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
int MEMORY_RW(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags)
{
#ifndef MEM_USERLAND
	int ok = 1;
#endif
	uint64_t paddr;
	int cache, no_exceptions, offset;
	unsigned char *memblock;
#ifdef BINTRANS
	int bintrans_cached = cpu->machine->bintrans_enable;
#endif
	no_exceptions = cache_flags & NO_EXCEPTIONS;
	cache = cache_flags & CACHE_FLAGS_MASK;

#ifdef MEM_PPC
	if (cpu->cd.ppc.bits == 32)
		vaddr &= 0xffffffff;
#endif

#ifdef MEM_MIPS
#ifdef BINTRANS
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->cd.mips.pc_bintrans_host_4kpage = NULL;
			cpu->cd.mips.pc_bintrans_paddr_valid = 0;
		}
	}
#endif
#endif	/*  MEM_MIPS  */

#ifdef MEM_USERLAND
	paddr = vaddr & 0x7fffffff;
	goto have_paddr;
#endif

#ifndef MEM_USERLAND
#ifdef MEM_MIPS
	/*
	 *  For instruction fetch, are we on the same page as the last
	 *  instruction we fetched?
	 *
	 *  NOTE: There's no need to check this stuff here if this address
	 *  is known to be in host ram, as it's done at instruction fetch
	 *  time in cpu.c!  Only check if _host_4k_page == NULL.
	 */
	if (cache == CACHE_INSTRUCTION &&
	    cpu->cd.mips.pc_last_host_4k_page == NULL &&
	    (vaddr & ~0xfff) == cpu->cd.mips.pc_last_virtual_page) {
		paddr = cpu->cd.mips.pc_last_physical_page | (vaddr & 0xfff);
		goto have_paddr;
	}
#endif	/*  MEM_MIPS  */

	if (cache_flags & PHYSICAL || cpu->translate_address == NULL) {
		paddr = vaddr;
	} else {
		ok = cpu->translate_address(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) +
		    (no_exceptions? FLAG_NOEXCEPTIONS : 0)
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));
		/*  If the translation caused an exception, or was invalid in
		    some way, we simply return without doing the memory
		    access:  */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}


#ifdef MEM_MIPS
	/*
	 *  If correct cache emulation is enabled, and we need to simluate
	 *  cache misses even from the instruction cache, we can't run directly
	 *  from a host page. :-/
	 */
#if defined(ENABLE_CACHE_EMULATION) && defined(ENABLE_INSTRUCTION_DELAYS)
#else
	if (cache == CACHE_INSTRUCTION) {
		cpu->cd.mips.pc_last_virtual_page = vaddr & ~0xfff;
		cpu->cd.mips.pc_last_physical_page = paddr & ~0xfff;
		cpu->cd.mips.pc_last_host_4k_page = NULL;

		/*  _last_host_4k_page will be set to 1 further down,
		    if the page is actually in host ram  */
	}
#endif
#endif	/*  MEM_MIPS  */
#endif	/*  ifndef MEM_USERLAND  */


have_paddr:


#ifdef MEM_MIPS
	/*  TODO: How about bintrans vs cache emulation?  */
#ifdef BINTRANS
	if (bintrans_cached) {
		if (cache == CACHE_INSTRUCTION) {
			cpu->cd.mips.pc_bintrans_paddr_valid = 1;
			cpu->cd.mips.pc_bintrans_paddr = paddr;
		}
	}
#endif
#endif	/*  MEM_MIPS  */


	if (!(cache_flags & PHYSICAL))
		if (no_exceptions)
			goto no_exception_access;


#ifndef MEM_USERLAND
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
				if (bintrans_cached && mem->dev_flags[i] &
				    MEM_BINTRANS_OK) {
					int wf = writeflag == MEM_WRITE? 1 : 0;

					if (writeflag) {
						if (paddr < mem->
						    dev_bintrans_write_low[i])
							mem->
							dev_bintrans_write_low
							    [i] =
							    paddr & ~0xfff;
						if (paddr > mem->
						    dev_bintrans_write_high[i])
							mem->
						 	dev_bintrans_write_high
							    [i] = paddr | 0xfff;
					}

					if (!(mem->dev_flags[i] &
					    MEM_BINTRANS_WRITE_OK))
						wf = 0;

					update_translation_table(cpu,
					    vaddr & ~0xfff,
					    mem->dev_bintrans_data[i] +
					    (paddr & ~0xfff),
					    wf, orig_paddr & ~0xfff);
				}
#endif

				res = mem->dev_f[i](cpu, mem, paddr, data, len,
				    writeflag, mem->dev_extra[i]);

#ifdef ENABLE_INSTRUCTION_DELAYS
				if (res == 0)
					res = -1;

				cpu->cd.mips.instruction_delay +=
				    ( (abs(res) - 1) *
				     cpu->cd.mips.cpu_type.instrs_per_cycle );
#endif
				/*
				 *  If accessing the memory mapped device
				 *  failed, then return with a DBE exception.
				 */
				if (res <= 0) {
					debug("%s device '%s' addr %08lx "
					    "failed\n", writeflag?
					    "writing to" : "reading from",
					    mem->dev_name[i], (long)paddr);
#ifdef MEM_MIPS
					mips_cpu_exception(cpu, EXCEPTION_DBE,
					    0, vaddr, 0, 0, 0, 0);
#endif
					return MEMORY_ACCESS_FAILED;
				}

				goto do_return_ok;
			}

			i ++;
			if (i == mem->n_mmapped_devices)
				i = 0;
		} while (i != start);
	}


#ifdef MEM_MIPS
	/*
	 *  Data and instruction cache emulation:
	 */

	switch (cpu->cd.mips.cpu_type.mmu_model) {
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
/*		if ((cpu->cd.mips.coproc[0]->reg[COP0_CONFIG] & 7) == 3) {  */
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
#endif	/*  MEM_MIPS  */


	/*  Outside of physical RAM?  */
	if (paddr >= mem->physical_max) {
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
#ifdef MEM_MIPS
				uint64_t offset;
#endif
				if (!quiet_mode) {
					fatal("[ memory_rw(): writeflag=%i ",
					    writeflag);
					if (writeflag) {
						unsigned int i;
						debug("data={", writeflag);
						if (len > 16) {
							int start2 = len-16;
							for (i=0; i<16; i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
							debug(" .. ");
							if (start2 < 16)
								start2 = 16;
							for (i=start2; i<len;
							    i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
						} else
							for (i=0; i<len; i++)
								debug("%s%02x",
								    i?",":"",
								    data[i]);
						debug("}");
					}
#ifdef MEM_MIPS
					symbol = get_symbol_name(
					    &cpu->machine->symbol_context,
					    cpu->cd.mips.pc_last, &offset);
#else
					symbol = "(unimpl for non-MIPS)";
#endif
					fatal(" paddr=%llx >= physical_max pc="
					    "0x%08llx <%s> ]\n",
					    (long long)paddr,
					    (long long)cpu->cd.mips.pc_last,
					    symbol? symbol : "no symbol");
				}

				if (cpu->machine->single_step_on_bad_addr) {
					uint64_t pc = 0;
#ifdef MEM_MIPS
					pc = cpu->cd.mips.pc;
#endif
					fatal("[ unimplemented access to "
					    "0x%016llx, pc = 0x%016llx ]\n",
					    (long long)paddr, (long long)pc);
					single_step = 1;
				}
			}

			if (writeflag == MEM_READ) {
				/*  Return all zeroes? (Or 0xff? TODO)  */
				memset(data, 0, len);

#ifdef MEM_MIPS
				/*
				 *  For real data/instruction accesses, cause
				 *  an exceptions on an illegal read:
				 */
				if (cache != CACHE_NONE && cpu->machine->
				    dbe_on_nonexistant_memaccess) {
					if (paddr >= mem->physical_max &&
					    paddr < mem->physical_max+1048576)
						mips_cpu_exception(cpu,
						    EXCEPTION_DBE, 0, vaddr, 0,
						    0, 0, 0);
				}
#endif  /*  MEM_MIPS  */
			}

			/*  Hm? Shouldn't there be a DBE exception for
			    invalid writes as well?  TODO  */

			goto do_return_ok;
		}
	}

#endif	/*  ifndef MEM_USERLAND  */


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
			cpu->cd.mips.pc_last_host_4k_page = memblock
			    + (offset & ~0xfff);
#ifdef BINTRANS
			if (bintrans_cached) {
				cpu->cd.mips.pc_bintrans_host_4kpage =
				    cpu->cd.mips.pc_last_host_4k_page;
			}
#endif
		}
	}


do_return_ok:
	return MEMORY_ACCESS_OK;
}


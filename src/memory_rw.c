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
 *  $Id: memory_rw.c,v 1.87 2006-06-22 11:43:03 debug Exp $
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
 *	misc_flags	CACHE_{NONE,DATA,INSTRUCTION} | other flags
 *
 *  If the address indicates access to a memory mapped device, that device'
 *  read/write access function is called.
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
	unsigned char *data, size_t len, int writeflag, int misc_flags)
{
#ifdef MEM_ALPHA
	const int offset_mask = 0x1fff;
#else
	const int offset_mask = 0xfff;
#endif

#ifndef MEM_USERLAND
	int ok = 1;
#endif
	uint64_t paddr;
	int cache, no_exceptions, offset;
	unsigned char *memblock;
	int dyntrans_device_danger = 0;

	no_exceptions = misc_flags & NO_EXCEPTIONS;
	cache = misc_flags & CACHE_FLAGS_MASK;

#ifdef MEM_X86
	/*  Real-mode wrap-around:  */
	if (REAL_MODE && !(misc_flags & PHYSICAL)) {
		if ((vaddr & 0xffff) + len > 0x10000) {
			/*  Do one byte at a time:  */
			int res = 0;
			size_t i;
			for (i=0; i<len; i++)
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    writeflag, misc_flags);
			return res;
		}
	}

	/*  Crossing a page boundary? Then do one byte at a time:  */
	if ((vaddr & 0xfff) + len > 0x1000 && !(misc_flags & PHYSICAL)
	    && cpu->cd.x86.cr[0] & X86_CR0_PG) {
		/*  For WRITES: Read ALL BYTES FIRST and write them back!!!
		    Then do a write of all the new bytes. This is to make sure
		    than both pages around the boundary are writable so we don't
		    do a partial write.  */
		int res = 0;
		size_t i;
		if (writeflag == MEM_WRITE) {
			unsigned char tmp;
			for (i=0; i<len; i++) {
				res = MEMORY_RW(cpu, mem, vaddr+i, &tmp, 1,
				    MEM_READ, misc_flags);
				if (!res)
					return 0;
				res = MEMORY_RW(cpu, mem, vaddr+i, &tmp, 1,
				    MEM_WRITE, misc_flags);
				if (!res)
					return 0;
			}
			for (i=0; i<len; i++) {
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    MEM_WRITE, misc_flags);
				if (!res)
					return 0;
			}
		} else {
			for (i=0; i<len; i++) {
				/*  Do one byte at a time:  */
				res = MEMORY_RW(cpu, mem, vaddr+i, &data[i], 1,
				    writeflag, misc_flags);
				if (!res) {
					if (cache == CACHE_INSTRUCTION) {
						fatal("FAILED instruction "
						    "fetch across page boundar"
						    "y: todo. vaddr=0x%08x\n",
						    (int)vaddr);
						cpu->running = 0;
					}
					return 0;
				}
			}
		}
		return res;
	}
#endif	/*  X86  */


#ifdef MEM_USERLAND
#ifdef MEM_ALPHA
	paddr = vaddr;
#else
	paddr = vaddr & 0x7fffffff;
#endif
#else	/*  !MEM_USERLAND  */
	if (misc_flags & PHYSICAL || cpu->translate_address == NULL) {
		paddr = vaddr;
	} else {
		ok = cpu->translate_address(cpu, vaddr, &paddr,
		    (writeflag? FLAG_WRITEFLAG : 0) +
		    (no_exceptions? FLAG_NOEXCEPTIONS : 0)
#ifdef MEM_X86
		    + (misc_flags & NO_SEGMENTATION)
#endif
#ifdef MEM_ARM
		    + (misc_flags & MEMORY_USER_ACCESS)
#endif
		    + (cache==CACHE_INSTRUCTION? FLAG_INSTR : 0));
		/*  If the translation caused an exception, or was invalid in
		    some way, we simply return without doing the memory
		    access:  */
		if (!ok)
			return MEMORY_ACCESS_FAILED;
	}


#ifdef MEM_X86
	/*  DOS debugging :-)  */
	if (!quiet_mode && !(misc_flags & PHYSICAL)) {
		if (paddr >= 0x400 && paddr <= 0x4ff)
			debug("{ PC BIOS DATA AREA: %s 0x%x }\n", writeflag ==
			    MEM_WRITE? "writing to" : "reading from",
			    (int)paddr);
#if 0
		if (paddr >= 0xf0000 && paddr <= 0xfffff)
			debug("{ BIOS ACCESS: %s 0x%x }\n",
			    writeflag == MEM_WRITE? "writing to" :
			    "reading from", (int)paddr);
#endif
	}
#endif
#endif	/*  !MEM_USERLAND  */


#ifndef MEM_USERLAND
	/*
	 *  Memory mapped device?
	 *
	 *  TODO: if paddr < base, but len enough, then the device should
	 *  still be written to!
	 */
	if (paddr >= mem->mmap_dev_minaddr && paddr < mem->mmap_dev_maxaddr) {
		uint64_t orig_paddr = paddr;
		int i, start, end, res;

		/*
		 *  Really really slow, but unfortunately necessary. This is
		 *  to avoid the folowing scenario:
		 *
		 *	a) offsets 0x000..0x123 are normal memory
		 *	b) offsets 0x124..0x777 are a device
		 *
		 *	1) a read is done from offset 0x100. the page is
		 *	   added to the dyntrans system as a "RAM" page
		 *	2) a dyntranslated read is done from offset 0x200,
		 *	   which should access the device, but since the
		 *	   entire page is added, it will access non-existant
		 *	   RAM instead, without warning.
		 *
		 *  Setting dyntrans_device_danger = 1 on accesses which are
		 *  on _any_ offset on pages that are device mapped avoids
		 *  this problem, but it is probably not very fast.
		 *
		 *  TODO: Convert this into a quick (multi-level, 64-bit)
		 *  address space lookup, to find dangerous pages.
		 */
#if 1
		for (i=0; i<mem->n_mmapped_devices; i++)
			if (paddr >= (mem->dev_baseaddr[i] & ~offset_mask) &&
			    paddr <= ((mem->dev_endaddr[i]-1) | offset_mask)) {
				dyntrans_device_danger = 1;
				break;
			}
#endif

		start = 0; end = mem->n_mmapped_devices - 1;
		i = mem->last_accessed_device;

		/*  Scan through all devices:  */
		do {
			if (paddr >= mem->dev_baseaddr[i] &&
			    paddr < mem->dev_endaddr[i]) {
				/*  Found a device, let's access it:  */
				mem->last_accessed_device = i;

				paddr -= mem->dev_baseaddr[i];
				if (paddr + len > mem->dev_length[i])
					len = mem->dev_length[i] - paddr;

				if (cpu->update_translation_table != NULL &&
				    !(ok & MEMORY_NOT_FULL_PAGE) &&
				    mem->dev_flags[i] & DM_DYNTRANS_OK) {
					int wf = writeflag == MEM_WRITE? 1 : 0;
					unsigned char *host_addr;

					if (!(mem->dev_flags[i] &
					    DM_DYNTRANS_WRITE_OK))
						wf = 0;

					if (writeflag && wf) {
						if (paddr < mem->
						    dev_dyntrans_write_low[i])
							mem->
							dev_dyntrans_write_low
							    [i] = paddr &
							    ~offset_mask;
						if (paddr >= mem->
						    dev_dyntrans_write_high[i])
							mem->
						 	dev_dyntrans_write_high
							    [i] = paddr |
							    offset_mask;
					}

					if (mem->dev_flags[i] &
					    DM_EMULATED_RAM) {
						/*  MEM_WRITE to force the page
						    to be allocated, if it
						    wasn't already  */
						uint64_t *pp = (uint64_t *)
						    mem->dev_dyntrans_data[i];
						uint64_t p = orig_paddr - *pp;
						host_addr =
						    memory_paddr_to_hostaddr(
						    mem, p, MEM_WRITE)
						    + (p & ~offset_mask
						    & ((1 <<
						    BITS_PER_MEMBLOCK) - 1));
					} else {
						host_addr =
						    mem->dev_dyntrans_data[i] +
						    (paddr & ~offset_mask);
					}
					cpu->update_translation_table(cpu,
					    vaddr & ~offset_mask, host_addr,
					    wf, orig_paddr & ~offset_mask);
				}

				res = 0;
				if (!no_exceptions || (mem->dev_flags[i] &
				    DM_READS_HAVE_NO_SIDE_EFFECTS))
					res = mem->dev_f[i](cpu, mem, paddr,
					    data, len, writeflag,
					    mem->dev_extra[i]);

				if (res == 0)
					res = -1;

#ifndef MEM_X86
				/*
				 *  If accessing the memory mapped device
				 *  failed, then return with a DBE exception.
				 */
				if (res <= 0 && !no_exceptions) {
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
#endif
				goto do_return_ok;
			}

			if (paddr < mem->dev_baseaddr[i])
				end = i - 1;
			if (paddr >= mem->dev_endaddr[i])
				start = i + 1;
			i = (start + end) >> 1;
		} while (start <= end);
	}


#ifdef MEM_MIPS
	/*
	 *  Data and instruction cache emulation:
	 */

	switch (cpu->cd.mips.cpu_type.mmu_model) {
	case MMU3K:
		/*  if not uncached addess  (TODO: generalize this)  */
		if (!(misc_flags & PHYSICAL) && cache != CACHE_NONE &&
		    !((vaddr & 0xffffffffULL) >= 0xa0000000ULL &&
		      (vaddr & 0xffffffffULL) <= 0xbfffffffULL)) {
			if (memory_cache_R3000(cpu, cache, paddr,
			    writeflag, len, data))
				goto do_return_ok;
		}
		break;
	default:
		/*  R4000 etc  */
		/*  TODO  */
		;
	}
#endif	/*  MEM_MIPS  */


	/*  Outside of physical RAM?  */
	if (paddr >= mem->physical_max) {
#ifdef MEM_MIPS
		if ((paddr & 0xffffc00000ULL) == 0x1fc00000) {
			/*  Ok, this is PROM stuff  */
		} else if ((paddr & 0xfffff00000ULL) == 0x1ff00000) {
			/*  Sprite reads from this area of memory...  */
			/*  TODO: is this still correct?  */
			if (writeflag == MEM_READ)
				memset(data, 0, len);
			goto do_return_ok;
		} else
#endif /* MIPS */
		{
			if (paddr >= mem->physical_max) {
				uint64_t offset, old_pc = cpu->pc;
				char *symbol;

				/*  This allows for example OS kernels to probe
				    memory a few KBs past the end of memory,
				    without giving too many warnings.  */
				if (!quiet_mode && !no_exceptions && paddr >=
				    mem->physical_max + 0x40000) {
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

					fatal(" paddr=0x%llx >= physical_max"
					    "; pc=", (long long)paddr);
					if (cpu->is_32bit)
						fatal("0x%08x",(int)old_pc);
					else
						fatal("0x%016llx",
						    (long long)old_pc);
					symbol = get_symbol_name(
					    &cpu->machine->symbol_context,
					    old_pc, &offset);
					fatal(" <%s> ]\n",
					    symbol? symbol : " no symbol ");
				}
			}

			if (writeflag == MEM_READ) {
#ifdef MEM_X86
				/*  Reading non-existant memory on x86:  */
				memset(data, 0xff, len);
#else
				/*  Return all zeroes? (Or 0xff? TODO)  */
				memset(data, 0, len);
#endif

#ifdef MEM_MIPS
				/*
				 *  For real data/instruction accesses, cause
				 *  an exceptions on an illegal read:
				 */
				if (cache != CACHE_NONE && cpu->machine->
				    dbe_on_nonexistant_memaccess &&
				    !no_exceptions) {
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


	/*
	 *  Uncached access:
	 *
	 *  1)  Translate the physical address to a host address.
	 *
	 *  2)  Insert this virtual->physical->host translation into the
	 *      fast translation arrays (using update_translation_table()).
	 *
	 *  3)  If this was a Write, then invalidate any code translations
	 *      in that page.
	 */
	memblock = memory_paddr_to_hostaddr(mem, paddr, writeflag);
	if (memblock == NULL) {
		if (writeflag == MEM_READ)
			memset(data, 0, len);
		goto do_return_ok;
	}

	offset = paddr & ((1 << BITS_PER_MEMBLOCK) - 1);

	if (cpu->update_translation_table != NULL && !dyntrans_device_danger
#ifndef MEM_MIPS
/*	    && !(misc_flags & MEMORY_USER_ACCESS)  */
#ifndef MEM_USERLAND
	    && !(ok & MEMORY_NOT_FULL_PAGE)
#endif
#endif
	    && !no_exceptions)
		cpu->update_translation_table(cpu, vaddr & ~offset_mask,
		    memblock + (offset & ~offset_mask),
		    (misc_flags & MEMORY_USER_ACCESS) |
#ifndef MEM_MIPS
		    (cache == CACHE_INSTRUCTION? TLB_CODE : 0) |
#endif
#if !defined(MEM_MIPS) && !defined(MEM_USERLAND)
		    (cache == CACHE_INSTRUCTION?
			(writeflag == MEM_WRITE? 1 : 0) : ok - 1),
#else
		    (writeflag == MEM_WRITE? 1 : 0),
#endif
		    paddr & ~offset_mask);

	/*  Invalidate code translations for the page we are writing to.  */
	if (writeflag == MEM_WRITE && cpu->invalidate_code_translation != NULL)
		cpu->invalidate_code_translation(cpu, paddr, INVALIDATE_PADDR);

	if (writeflag == MEM_WRITE) {
		/*  Ugly optimization, but it works:  */
		if (len == sizeof(uint32_t) && (offset & 3)==0
		    && ((size_t)data&3)==0)
			*(uint32_t *)(memblock + offset) = *(uint32_t *)data;
		else if (len == sizeof(uint8_t))
			*(uint8_t *)(memblock + offset) = *(uint8_t *)data;
		else
			memcpy(memblock + offset, data, len);
	} else {
		/*  Ugly optimization, but it works:  */
		if (len == sizeof(uint32_t) && (offset & 3)==0
		    && ((size_t)data&3)==0)
			*(uint32_t *)data = *(uint32_t *)(memblock + offset);
		else if (len == sizeof(uint8_t))
			*(uint8_t *)data = *(uint8_t *)(memblock + offset);
		else
			memcpy(data, memblock + offset, len);
	}


do_return_ok:
	return MEMORY_ACCESS_OK;
}


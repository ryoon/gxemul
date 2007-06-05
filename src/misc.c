/*
 *  Copyright (C) 2005-2007  Anders Gavare.  All rights reserved.
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
 *  $Id: misc.c,v 1.7 2007-06-05 05:40:24 debug Exp $
 *
 *  This file contains things that don't fit anywhere else, and fake/dummy
 *  implementations of libc functions that are missing on some systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "cpu.h"
#include "memory.h"
#include "misc.h"


/*
 *  mystrtoull():
 *
 *  This function is used on OSes that don't have strtoull() in libc.
 */
unsigned long long mystrtoull(const char *s, char **endp, int base)
{
	unsigned long long res = 0;
	int minus_sign = 0;

	if (s == NULL)
		return 0;

	/*  TODO: Implement endp?  */
	if (endp != NULL) {
		fprintf(stderr, "mystrtoull(): endp isn't implemented\n");
		exit(1);
	}

	if (s[0] == '-') {
		minus_sign = 1;
		s++;
	}

	/*  Guess base:  */
	if (base == 0) {
		if (s[0] == '0') {
			/*  Just "0"? :-)  */
			if (!s[1])
				return 0;
			if (s[1] == 'x' || s[1] == 'X') {
				base = 16;
				s += 2;
			} else {
				base = 8;
				s ++;
			}
		} else if (s[0] >= '1' && s[0] <= '9')
			base = 10;
	}

	while (s[0]) {
		int c = s[0];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			c = c - 'A' + 10;
		else
			break;
		switch (base) {
		case 8:	res = (res << 3) | c;
			break;
		case 16:res = (res << 4) | c;
			break;
		default:res = (res * base) + c;
		}
		s++;
	}

	if (minus_sign)
		res = (uint64_t) -(int64_t)res;
	return res;
}


/*
 *  mymkstemp():
 *
 *  mkstemp() replacement for systems that lack that function. This is NOT
 *  really safe, but should at least allow the emulator to build and run.
 */
int mymkstemp(char *template)
{
	int h = 0;
	char *p = template;

	while (*p) {
		if (*p == 'X')
			*p = 48 + random() % 10;
		p++;
	}

	h = open(template, O_RDWR, 0600);
	return h;
}


#ifdef USE_STRLCPY_REPLACEMENTS
/*
 *  mystrlcpy():
 *
 *  Quick hack strlcpy() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcpy(char *dst, const char *src, size_t size)
{
	strcpy(dst, src);
	return strlen(src);
}


/*
 *  mystrlcat():
 *
 *  Quick hack strlcat() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcat(char *dst, const char *src, size_t size)
{
	size_t orig_dst_len = strlen(dst);
	strcat(dst, src);
	return strlen(src) + orig_dst_len;
}
#endif

/*
 *  dump_mem_string():
 *
 *  Dump the contents of emulated RAM as readable text.  Bytes that aren't
 *  readable are dumped in [xx] notation, where xx is in hexadecimal.
 *  Dumping ends after DUMP_MEM_STRING_MAX bytes, or when a terminating
 *  zero byte is found.
 */
#define DUMP_MEM_STRING_MAX	45
void dump_mem_string(struct cpu *cpu, uint64_t addr)
{
	int i;
	for (i=0; i<DUMP_MEM_STRING_MAX; i++) {
		unsigned char ch = '\0';

		cpu->memory_rw(cpu, cpu->mem, addr + i, &ch, sizeof(ch),
		    MEM_READ, CACHE_DATA | NO_EXCEPTIONS);
		if (ch == '\0')
			return;
		if (ch >= ' ' && ch < 126)
			debug("%c", ch);  
		else
			debug("[%02x]", ch);
	}
}


/*
 *  store_byte():
 *
 *  Stores a byte in emulated ram. (Helper function.)
 */
void store_byte(struct cpu *cpu, uint64_t addr, uint8_t data)
{
	if ((addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	cpu->memory_rw(cpu, cpu->mem,
	    addr, &data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_string():
 *
 *  Stores chars into emulated RAM until a zero byte (string terminating
 *  character) is found. The zero byte is also copied.
 *  (strcpy()-like helper function, host-RAM-to-emulated-RAM.)
 */
void store_string(struct cpu *cpu, uint64_t addr, char *s)
{
	do {
		store_byte(cpu, addr++, *s);
	} while (*s++);
}


/*
 *  add_environment_string():
 *
 *  Like store_string(), but advances the pointer afterwards. The most
 *  obvious use is to place a number of strings (such as environment variable
 *  strings) after one-another in emulated memory.
 */
void add_environment_string(struct cpu *cpu, char *s, uint64_t *addr)
{
	store_string(cpu, *addr, s);
	(*addr) += strlen(s) + 1;
}


/*
 *  add_environment_string_dual():
 *
 *  Add "dual" environment strings, one for the variable name and one for the
 *  value, and update pointers afterwards.
 */
void add_environment_string_dual(struct cpu *cpu,
	uint64_t *ptrp, uint64_t *addrp, char *s1, char *s2)
{
	uint64_t ptr = *ptrp, addr = *addrp;

	store_32bit_word(cpu, ptr, addr);
	ptr += sizeof(uint32_t);
	if (addr != 0) {
		store_string(cpu, addr, s1);
		addr += strlen(s1) + 1;
	}
	store_32bit_word(cpu, ptr, addr);
	ptr += sizeof(uint32_t);
	if (addr != 0) {
		store_string(cpu, addr, s2);
		addr += strlen(s2) + 1;
	}

	*ptrp = ptr;
	*addrp = addr;
}


/*
 *  store_64bit_word():
 *
 *  Stores a 64-bit word in emulated RAM.  Byte order is taken into account.
 *  Helper function.
 */
int store_64bit_word(struct cpu *cpu, uint64_t addr, uint64_t data64)
{
	unsigned char data[8];
	if ((addr >> 32) == 0)
		addr = (int64_t)(int32_t)addr;
	data[0] = (data64 >> 56) & 255;
	data[1] = (data64 >> 48) & 255;
	data[2] = (data64 >> 40) & 255;
	data[3] = (data64 >> 32) & 255;
	data[4] = (data64 >> 24) & 255;
	data[5] = (data64 >> 16) & 255;
	data[6] = (data64 >> 8) & 255;
	data[7] = (data64) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[7]; data[7] = tmp;
		tmp = data[1]; data[1] = data[6]; data[6] = tmp;
		tmp = data[2]; data[2] = data[5]; data[5] = tmp;
		tmp = data[3]; data[3] = data[4]; data[4] = tmp;
	}
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_32bit_word():
 *
 *  Stores a 32-bit word in emulated RAM.  Byte order is taken into account.
 *  (This function takes a 64-bit word as argument, to suppress some
 *  warnings, but only the lowest 32 bits are used.)
 */
int store_32bit_word(struct cpu *cpu, uint64_t addr, uint64_t data32)
{
	unsigned char data[4];

	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_16bit_word():
 *
 *  Stores a 16-bit word in emulated RAM.  Byte order is taken into account.
 *  (This function takes a 64-bit word as argument, to suppress some
 *  warnings, but only the lowest 16 bits are used.)
 */
int store_16bit_word(struct cpu *cpu, uint64_t addr, uint64_t data16)
{
	unsigned char data[2];

	data[0] = (data16 >> 8) & 255;
	data[1] = (data16) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}
	return cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_WRITE, CACHE_DATA);
}


/*
 *  store_buf():
 *
 *  memcpy()-like helper function, from host RAM to emulated RAM.
 */
void store_buf(struct cpu *cpu, uint64_t addr, char *s, size_t len)
{
	size_t psize = 1024;	/*  1024 256 64 16 4 1  */

	while (len != 0) {
		if ((addr & (psize-1)) == 0) {
			while (len >= psize) {
				cpu->memory_rw(cpu, cpu->mem, addr,
				    (unsigned char *)s, psize, MEM_WRITE,
				    CACHE_DATA);
				addr += psize;
				s += psize;
				len -= psize;
			}
		}
		psize >>= 2;
	}

	while (len-- != 0)
		store_byte(cpu, addr++, *s++);
}


/*
 *  store_pointer_and_advance():
 *
 *  Stores a 32-bit or 64-bit pointer in emulated RAM, and advances the
 *  target address. (Useful for e.g. ARCBIOS environment initialization.)
 */
void store_pointer_and_advance(struct cpu *cpu, uint64_t *addrp,
	uint64_t data, int flag64)
{
	uint64_t addr = *addrp;
	if (flag64) {
		store_64bit_word(cpu, addr, data);
		addr += 8;
	} else {
		store_32bit_word(cpu, addr, data);
		addr += 4;
	}
	*addrp = addr;
}


/*
 *  load_64bit_word():
 *
 *  Helper function. Emulated byte order is taken into account.
 */
uint64_t load_64bit_word(struct cpu *cpu, uint64_t addr)
{
	unsigned char data[8];

	cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[7]; data[7] = tmp;
		tmp = data[1]; data[1] = data[6]; data[6] = tmp;
		tmp = data[2]; data[2] = data[5]; data[5] = tmp;
		tmp = data[3]; data[3] = data[4]; data[4] = tmp;
	}

	return
	    ((uint64_t)data[0] << 56) + ((uint64_t)data[1] << 48) +
	    ((uint64_t)data[2] << 40) + ((uint64_t)data[3] << 32) +
	    ((uint64_t)data[4] << 24) + ((uint64_t)data[5] << 16) +
	    ((uint64_t)data[6] << 8) + (uint64_t)data[7];
}


/*
 *  load_32bit_word():
 *
 *  Helper function. Emulated byte order is taken into account.
 */
uint32_t load_32bit_word(struct cpu *cpu, uint64_t addr)
{
	unsigned char data[4];

	cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}

	return (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
}


/*
 *  load_16bit_word():
 *
 *  Helper function. Emulated byte order is taken into account.
 */
uint16_t load_16bit_word(struct cpu *cpu, uint64_t addr)
{
	unsigned char data[2];

	cpu->memory_rw(cpu, cpu->mem,
	    addr, data, sizeof(data), MEM_READ, CACHE_DATA);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}

	return (data[0] << 8) + data[1];
}


/*
 *  store_64bit_word_in_host():
 *
 *  Stores a 64-bit word in the _host's_ RAM.  Emulated byte order is taken
 *  into account.  This is useful when building structs in the host's RAM
 *  which will later be copied into emulated RAM.
 */
void store_64bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint64_t data64)
{
	data[0] = (data64 >> 56) & 255;
	data[1] = (data64 >> 48) & 255;
	data[2] = (data64 >> 40) & 255;
	data[3] = (data64 >> 32) & 255;
	data[4] = (data64 >> 24) & 255;
	data[5] = (data64 >> 16) & 255;
	data[6] = (data64 >> 8) & 255;
	data[7] = (data64) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[7]; data[7] = tmp;
		tmp = data[1]; data[1] = data[6]; data[6] = tmp;
		tmp = data[2]; data[2] = data[5]; data[5] = tmp;
		tmp = data[3]; data[3] = data[4]; data[4] = tmp;
	}
}


/*
 *  store_32bit_word_in_host():
 *
 *  See comment for store_64bit_word_in_host().
 *
 *  (Note:  The data32 parameter is a uint64_t. This is done to suppress
 *  some warnings.)
 */
void store_32bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint64_t data32)
{
	data[0] = (data32 >> 24) & 255;
	data[1] = (data32 >> 16) & 255;
	data[2] = (data32 >> 8) & 255;
	data[3] = (data32) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[3]; data[3] = tmp;
		tmp = data[1]; data[1] = data[2]; data[2] = tmp;
	}
}


/*
 *  store_16bit_word_in_host():
 *
 *  See comment for store_64bit_word_in_host().
 */
void store_16bit_word_in_host(struct cpu *cpu,
	unsigned char *data, uint16_t data16)
{
	data[0] = (data16 >> 8) & 255;
	data[1] = (data16) & 255;
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
		int tmp = data[0]; data[0] = data[1]; data[1] = tmp;
	}
}


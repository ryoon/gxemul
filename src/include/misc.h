#ifndef	MISC_H
#define	MISC_H

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
 *  $Id: misc.h,v 1.226 2005-03-05 12:34:02 debug Exp $
 *
 *  Misc. definitions for gxemul.
 */


#include <sys/types.h>
#include <inttypes.h>

/*
 *  ../config.h contains #defines set by the configure script. Some of these
 *  might reduce speed of the emulator, so don't enable them unless you
 *  need them.
 */

#include "../config.h"

/*  
 *  ENABLE_INSTRUCTION_DELAYS should be defined on the cc commandline using
 *  -D if you want it. (This is done by ./configure --delays)
 *
 *  ALWAYS_SIGNEXTEND_32 is enabled by ./configure --always32
 */
#define USE_TINY_CACHE
/*  #define HALT_IF_PC_ZERO  */


#ifdef NO_MAP_ANON
#ifdef mmap
#undef mmap
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
static void *no_map_anon_mmap(void *addr, size_t len, int prot, int flags,
	int nonsense_fd, off_t offset)
{
	void *p;
	int fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open /dev/zero\n");
		exit(1);
	}

	printf("addr=%p len=%lli prot=0x%x flags=0x%x nonsense_fd=%i "
	    "offset=%16lli\n", addr, (long long) len, prot, flags,
	    nonsense_fd, (long long) offset);

	p = mmap(addr, len, prot, flags, fd, offset);

	printf("p = %p\n", p);

	/*  TODO: Close the descriptor?  */
	return p;
}
#define mmap no_map_anon_mmap
#endif


struct cpu;
struct emul;
struct machine;
struct memory;


#define NO_BYTE_ORDER_OVERRIDE          -1
#define EMUL_LITTLE_ENDIAN              0
#define EMUL_BIG_ENDIAN                 1


/*  Debug stuff:  */
#define	DEBUG_BUFSIZE		1024

#ifndef	DEFAULT_BINTRANS_SIZE_IN_MB
#define	DEFAULT_BINTRANS_SIZE_IN_MB	16
#endif


/*  dec_prom.c:  */
int decstation_prom_emul(struct cpu *cpu);


/*  file.c:  */
int file_n_executables_loaded(void);
void file_load(struct machine *machine, struct memory *mem,
	char *filename, uint64_t *entrypointp,
	int arch, uint64_t *gpp, int *byte_order, uint64_t *tocp);


/*  main.c:  */
void debug_indentation(int diff);
void debug(char *fmt, ...);
void fatal(char *fmt, ...);
unsigned long long mystrtoull(const char *s, char **endp, int base);


/*  ps2_bios.c:  */
int playstation2_sifbios_emul(struct cpu *cpu);


/*  useremul.c:  */
void useremul_setup(struct cpu *, int, char **);
void useremul_syscall(struct cpu *cpu, uint32_t code);
void useremul_name_to_useremul(struct cpu *, char *name,
	int *arch, char **machine_name, char **cpu_name);
void useremul_list_emuls(void);
void useremul_init(void);


#endif	/*  MISC_H  */

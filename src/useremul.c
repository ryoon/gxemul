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
 *  $Id: useremul.c,v 1.37 2005-02-15 06:25:36 debug Exp $
 *
 *  Userland (syscall) emulation.
 *
 *  TODO:
 *
 *	NetBSD/pmax:
 *		environment passing
 *		more syscalls
 *
 *	32-bit vs 64-bit problems? MIPS n32, o32, n64?
 *
 *	Dynamic ELFs?
 *
 *	Try to prefix "/emul/mips/" or similar to all filenames,
 *		and only if that fails, try the given filename
 *
 *	Automagic errno translation?
 *
 *	Memory allocation? mmap etc.
 *
 *	File descriptor (0,1,2) assumptions?
 *
 *
 *  This module needs more cleanup.
 *  -------------------------------
 *
 *
 *  NOTE:  This module (useremul.c) is just a quick hack to see if
 *         userland emulation works at all.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>

#include "cpu.h"
#include "cpu_mips.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "syscall_linux_ppc.h"
#include "syscall_netbsd.h"
#include "syscall_ultrix.h"
#include "sysctl_netbsd.h"

struct syscall_emul {
	char		*name;
	int		arch;
	char		*cpu_name;
	void		(*f)(struct cpu *, uint32_t);
	void		(*setup)(struct cpu *, int, char **);

	struct syscall_emul *next;
};

static struct syscall_emul *first_syscall_emul;

/*  Max length of strings passed using syscall parameters:  */
#define	MAXLEN		8192


/*
 *  useremul_setup():
 *
 *  Set up an emulated environment suitable for running userland code. The
 *  program should already have been loaded into memory when this function
 *  is called.
 */
void useremul_setup(struct cpu *cpu, int argc, char **host_argv)
{
	struct syscall_emul *sep;

	sep = first_syscall_emul;

	while (sep != NULL) {
		if (strcasecmp(cpu->machine->userland_emul, sep->name) == 0) {
			sep->setup(cpu, argc, host_argv);
			return;
		}
		sep = sep->next;
	}

	fatal("useremul_setup(): internal error, unimplemented emulation?\n");
	exit(1);
}


/*
 *  useremul__linux_setup():
 *
 *  Set up an emulated userland environment suitable for running Linux
 *  binaries.
 */
void useremul__linux_setup(struct cpu *cpu, int argc, char **host_argv)
{
	debug("useremul__linux_setup(): TODO\n");

	/*  What is a good stack pointer? TODO  */
	cpu->cd.ppc.gpr[1] = 0x7ffff000ULL;
}


/*
 *  useremul__netbsd_setup():
 *
 *  Set up an emulated userland environment suitable for running NetBSD
 *  binaries.
 */
void useremul__netbsd_setup(struct cpu *cpu, int argc, char **host_argv)
{
	uint64_t stack_top = 0x7fff0000;
	uint64_t stacksize = 8 * 1048576;
	uint64_t stack_margin = 16384;
	uint64_t cur_argv;
	int i, i2;
	int envc = 1;

	switch (cpu->machine->arch) {
	case ARCH_MIPS:
		/*  See netbsd/sys/src/arch/mips/mips_machdep.c:setregs()  */
		cpu->cd.mips.gpr[MIPS_GPR_A0] = stack_top - stack_margin;
		cpu->cd.mips.gpr[25] = cpu->cd.mips.pc;		/*  reg. t9  */

		/*  The userland stack:  */
		cpu->cd.mips.gpr[MIPS_GPR_SP] = stack_top - stack_margin;
		add_symbol_name(&cpu->machine->symbol_context,
		    stack_top - stacksize, stacksize, "userstack", 0);

		/*  Stack contents:  (TODO: is this correct?)  */
		store_32bit_word(cpu, stack_top - stack_margin, argc);

		cur_argv = stack_top - stack_margin + 128 + (argc + envc)
		    * sizeof(uint32_t);
		for (i=0; i<argc; i++) {
			debug("adding argv[%i]: '%s'\n", i, host_argv[i]);

			store_32bit_word(cpu, stack_top - stack_margin +
			    4 + i*sizeof(uint32_t), cur_argv);
			store_string(cpu, cur_argv, host_argv[i]);
			cur_argv += strlen(host_argv[i]) + 1;
		}

		/*  Store a NULL value between the args and the environment strings:  */
		store_32bit_word(cpu, stack_top - stack_margin +
		    4 + i*sizeof(uint32_t), 0);  i++;

		/*  TODO: get environment strings from somewhere  */

		/*  Store all environment strings:  */
		for (i2 = 0; i2 < envc; i2 ++) {
			store_32bit_word(cpu, stack_top - stack_margin + 4
			    + (i+i2)*sizeof(uint32_t), cur_argv);
			store_string(cpu, cur_argv, "DISPLAY=localhost:0.0");
			cur_argv += strlen("DISPLAY=localhost:0.0") + 1;
		}
		break;

	case ARCH_PPC:
		debug("useremul__netbsd_setup(): TODO\n");

		/*  What is a good stack pointer? TODO  */
		cpu->cd.ppc.gpr[1] = 0x7ffff000ULL;

		break;

	default:
		fatal("useremul__netbsd_setup(): unimplemented arch\n");
		exit(1);
	}
}


/*
 *  useremul__ultrix_setup():
 *
 *  Set up an emulated userland environment suitable for running Ultrix
 *  binaries.
 */
void useremul__ultrix_setup(struct cpu *cpu, int argc, char **host_argv)
{
	uint64_t stack_top = 0x7fff0000;
	uint64_t stacksize = 8 * 1048576;
	uint64_t stack_margin = 16384;
	uint64_t cur_argv;
	int i, i2;
	int envc = 1;

	/*  TODO:  is this correct?  */
	cpu->cd.mips.gpr[MIPS_GPR_A0] = stack_top - stack_margin;
	cpu->cd.mips.gpr[25] = cpu->cd.mips.pc;		/*  reg. t9  */

	/*  The userland stack:  */
	cpu->cd.mips.gpr[MIPS_GPR_SP] = stack_top - stack_margin;
	add_symbol_name(&cpu->machine->symbol_context,
	    stack_top - stacksize, stacksize, "userstack", 0);

	/*  Stack contents:  (TODO: is this correct?)  */
	store_32bit_word(cpu, stack_top - stack_margin, argc);

	cur_argv = stack_top - stack_margin + 128 +
	    (argc + envc) * sizeof(uint32_t);
	for (i=0; i<argc; i++) {
		debug("adding argv[%i]: '%s'\n", i, host_argv[i]);

		store_32bit_word(cpu, stack_top - stack_margin +
		    4 + i*sizeof(uint32_t), cur_argv);
		store_string(cpu, cur_argv, host_argv[i]);
		cur_argv += strlen(host_argv[i]) + 1;
	}

	/*  Store a NULL value between the args and the environment strings:  */
	store_32bit_word(cpu, stack_top - stack_margin
	    + 4 + i*sizeof(uint32_t), 0);  i++;

	/*  TODO: get environment strings from somewhere  */

	/*  Store all environment strings:  */
	for (i2 = 0; i2 < envc; i2 ++) {
		store_32bit_word(cpu, stack_top - stack_margin + 4 +
		    (i+i2)*sizeof(uint32_t), cur_argv);
		store_string(cpu, cur_argv, "DISPLAY=localhost:0.0");
		cur_argv += strlen("DISPLAY=localhost:0.0") + 1;
	}
}


/*
 *  get_userland_string():
 *
 *  This can be used to retrieve strings, for example filenames,
 *  from the emulated memory.
 *
 *  NOTE: This function returns a pointer to a malloced buffer. It is up to
 *        the caller to use free().
 */
static unsigned char *get_userland_string(struct cpu *cpu, uint64_t baseaddr)
{
	unsigned char *charbuf;
	int i, len = 16384;

	charbuf = malloc(len);
	if (charbuf == NULL) {
		fprintf(stderr, "get_userland_string(): out of memory (trying"
		    " to allocate %i bytes)\n", len);
		exit(1);
	}

	/*  TODO: address validity check  */

	for (i=0; i<len; i++) {
		cpu->memory_rw(cpu, cpu->mem, baseaddr+i, charbuf+i,
		    1, MEM_READ, CACHE_DATA);
		if (charbuf[i] == '\0')
			break;
	}

	charbuf[MAXLEN-1] = 0;
	return charbuf;
}


/*
 *  get_userland_buf():
 *
 *  This can be used to retrieve buffers, for example inet_addr, from
 *  emulated memory.
 *
 *  NOTE: This function returns a pointer to a malloced buffer. It is up to
 *        the caller to use free().
 *
 *  TODO: combine this with get_userland_string() in some way
 */
static unsigned char *get_userland_buf(struct cpu *cpu,
	uint64_t baseaddr, int len)
{
	unsigned char *charbuf;
	int i;

	charbuf = malloc(len);
	if (charbuf == NULL) {
		fprintf(stderr, "get_userland_buf(): out of memory (trying"
		    " to allocate %i bytes)\n", len);
		exit(1);
	}

	/*  TODO: address validity check  */
	for (i=0; i<len; i++) {
		cpu->memory_rw(cpu, cpu->mem, baseaddr+i, charbuf+i, 1,
		    MEM_READ, CACHE_DATA);
		/*  debug(" %02x", charbuf[i]);  */
	}
	debug("\n");

	return charbuf;
}


/*
 *  useremul_syscall():
 *
 *  Handle userland syscalls.  This function is called whenever a userland
 *  process runs a 'syscall' instruction.  The code argument is the code
 *  embedded into the syscall instruction, if any.  (This 'code' value is not
 *  necessarily used by specific emulations.)
 */
void useremul_syscall(struct cpu *cpu, uint32_t code)
{
	if (cpu->useremul_syscall == NULL) {
		fatal("useremul_syscall(): cpu->useremul_syscall == NULL\n");
	} else
		cpu->useremul_syscall(cpu, code);
}


/*
 *  useremul__linux():
 *
 *  Linux syscall emulation.
 *
 *  TODO: How to make this work nicely with non-PPC archs.
 */
static void useremul__linux(struct cpu *cpu, uint32_t code)
{
	int nr;
	unsigned char *cp;
	uint64_t arg0, arg1, arg2, arg3;

	if (code != 0) {
		fatal("useremul__linux(): code %i: TODO\n", (int)code);
		exit(1);
	}

	nr = cpu->cd.ppc.gpr[0];
	arg0 = cpu->cd.ppc.gpr[3];
	arg1 = cpu->cd.ppc.gpr[4];
	arg2 = cpu->cd.ppc.gpr[5];
	arg3 = cpu->cd.ppc.gpr[6];

	switch (nr) {

	case LINUX_PPC_SYS_exit:
		debug("[ exit(%i) ]\n", (int)arg0);
		cpu->running = 0;
		break;

	case LINUX_PPC_SYS_write:
		debug("[ write(%i,0x%llx,%lli) ]\n",
		    (int)arg0, (long long)arg1, (long long)arg2);
		cp = get_userland_buf(cpu, arg1, arg2);
		write(arg0, cp, arg2);
		free(cp);
		break;

	default:
		fatal("useremul__linux(): syscall %i not yet implemented\n",
		    nr);
		cpu->running = 0;
	}
}


/*
 *  useremul__netbsd():
 *
 *  NetBSD syscall emulation.
 */
static void useremul__netbsd(struct cpu *cpu, uint32_t code)
{
	int error_flag = 0, result_high_set = 0;
	uint64_t arg0,arg1,arg2,arg3,stack0,stack1,stack2;
	int sysnr = 0;
	uint64_t error_code = 0;
	uint64_t result_low = 0;
	uint64_t result_high = 0;
	struct timeval tv;
	struct timezone tz;
	int descr;
	uint64_t length, mipsbuf, flags;
	unsigned char *charbuf;
	uint32_t sysctl_name, sysctl_namelen, sysctl_oldp,
	    sysctl_oldlenp, sysctl_newp, sysctl_newlen;
	uint32_t name0, name1, name2, name3;

	switch (cpu->machine->arch) {
	case ARCH_MIPS:
		sysnr = cpu->cd.mips.gpr[MIPS_GPR_V0];
		if (sysnr == NETBSD_SYS___syscall) {
			sysnr = cpu->cd.mips.gpr[MIPS_GPR_A0] +
			    (cpu->cd.mips.gpr[MIPS_GPR_A1] << 32);
			arg0 = cpu->cd.mips.gpr[MIPS_GPR_A2];
			arg1 = cpu->cd.mips.gpr[MIPS_GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			arg2 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 8);
			arg3 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 16);
			stack0 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 24);
			stack1 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 32);
			stack2 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 40);
		} else {
			arg0 = cpu->cd.mips.gpr[MIPS_GPR_A0];
			arg1 = cpu->cd.mips.gpr[MIPS_GPR_A1];
			arg2 = cpu->cd.mips.gpr[MIPS_GPR_A2];
			arg3 = cpu->cd.mips.gpr[MIPS_GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			stack0 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 4);
			stack1 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 8);
			stack2 = load_32bit_word(cpu,
			    cpu->cd.mips.gpr[MIPS_GPR_SP] + 12);
		}
		break;

	case ARCH_PPC:
		sysnr = cpu->cd.ppc.gpr[0];
		arg0 = cpu->cd.ppc.gpr[3];
		arg1 = cpu->cd.ppc.gpr[4];
		arg2 = cpu->cd.ppc.gpr[5];
		arg3 = cpu->cd.ppc.gpr[6];
		/*  TODO:  More arguments? Stack arguments?  */
		break;
	}

	/*
	 *  NOTE: The following code should not be CPU arch dependant!
	 *  (TODO)
	 */

	switch (sysnr) {

	case NETBSD_SYS_exit:
		debug("[ exit(%i) ]\n", (int)arg0);
		cpu->running = 0;
		cpu->machine->exit_without_entering_debugger = 1;
		break;

	case NETBSD_SYS_read:
		debug("[ read(%i,0x%llx,%lli) ]\n",
		    (int)arg0, (long long)arg1, (long long)arg2);

		if (arg2 != 0) {
			charbuf = malloc(arg2);
			if (charbuf == NULL) {
				fprintf(stderr, "out of memory in "
				    "useremul__netbsd()\n");
				exit(1);
			}
			result_low = read(arg0, charbuf, arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}

			/*  TODO: address validity check  */
			cpu->memory_rw(cpu, cpu->mem, arg1, charbuf,
			    arg2, MEM_WRITE, CACHE_DATA);
			free(charbuf);
		}
		break;

	case NETBSD_SYS_write:
		descr   = arg0;
		mipsbuf = arg1;
		length  = arg2;
		debug("[ write(%i,0x%llx,%lli) ]\n",
		    (int)descr, (long long)mipsbuf, (long long)length);
		if (length != 0) {
			charbuf = malloc(length);
			if (charbuf == NULL) {
				fprintf(stderr, "out of memory in "
				    "useremul__netbsd()\n");
				exit(1);
			}
			/*  TODO: address validity check  */
			cpu->memory_rw(cpu, cpu->mem, mipsbuf, charbuf,
			    length, MEM_READ, CACHE_DATA);
			result_low = write(descr, charbuf, length);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			free(charbuf);
		}
		break;

	case NETBSD_SYS_open:
		charbuf = get_userland_string(cpu, arg0);
		debug("[ open(\"%s\", 0x%llx, 0x%llx) ]\n",
		    charbuf, (long long)arg1, (long long)arg2);
		result_low = open((char *)charbuf, arg1, arg2);
		if ((int64_t)result_low < 0) {
			error_flag = 1;
			error_code = errno;
		}
		free(charbuf);
		break;

	case NETBSD_SYS_close:
		descr   = arg0;
		debug("[ close(%i) ]\n", (int)descr);
		error_code = close(descr);
		if (error_code != 0)
			error_flag = 1;
		break;

	case NETBSD_SYS_access:
		charbuf = get_userland_string(cpu, arg0);
		debug("[ access(\"%s\", 0x%llx) ]\n",
		    charbuf, (long long) arg1);
		result_low = access((char *)charbuf, arg1);
		if (result_low != 0) {
			error_flag = 1;
			error_code = errno;
		}
		free(charbuf);
		break;

	case NETBSD_SYS_getuid:
		debug("[ getuid() ]\n");
		result_low = getuid();
		break;

	case NETBSD_SYS_geteuid:
		debug("[ geteuid() ]\n");
		result_low = geteuid();
		break;

	case NETBSD_SYS_getgid:
		debug("[ getgid() ]\n");
		result_low = getgid();
		break;

	case NETBSD_SYS_getegid:
		debug("[ getegid() ]\n");
		result_low = getegid();
		break;

	case NETBSD_SYS_getfsstat:
		mipsbuf = arg0;
		length  = arg1;
		flags   = arg2;
		debug("[ getfsstat(0x%llx,%lli,0x%llx) ]\n",
		    (long long)mipsbuf, (long long)length,
		    (long long)flags);

		result_low = 0;		/*  nr of mounted filesystems,
					    for now  (TODO)  */

		/*  Fill in the struct statfs buffer at arg0...
			copy data from the host's getfsstat(). TODO  */
#if 1
		result_low = 1;
		store_32bit_word(cpu, mipsbuf + 0, 0);	/*  f_spare2  */
		store_32bit_word(cpu, mipsbuf + 4, 1024);	/*  f_bsize  */
		store_32bit_word(cpu, mipsbuf + 8, 65536);	/*  f_iosize  */
		store_32bit_word(cpu, mipsbuf + 12, 100);	/*  f_blocks  */
		store_32bit_word(cpu, mipsbuf + 16, 50);	/*  f_bfree  */
		store_32bit_word(cpu, mipsbuf + 20, 10);	/*  f_bavail  */
		store_32bit_word(cpu, mipsbuf + 24, 50);	/*  f_files  */
		store_32bit_word(cpu, mipsbuf + 28, 25);	/*  f_ffree  */
		store_32bit_word(cpu, mipsbuf + 28, 0x1234);	/*  f_fsid  */
		store_32bit_word(cpu, mipsbuf + 32, 0);	/*  f_owner  */
		store_32bit_word(cpu, mipsbuf + 36, 0);	/*  f_type  */
		store_32bit_word(cpu, mipsbuf + 40, 0);	/*  f_flags  */
		store_32bit_word(cpu, mipsbuf + 44, 0);	/*  f_fspare[0]  */
		store_32bit_word(cpu, mipsbuf + 48, 0);	/*  f_fspare[1]  */
		store_string(cpu, mipsbuf + 52, "ffs");	/*  f_typename  */
#define MFSNAMELEN 16
#define	MNAMELEN 90
		store_string(cpu, mipsbuf + 52 + MFSNAMELEN, "/");	/*  f_mntonname  */
		store_string(cpu, mipsbuf + 52 + MFSNAMELEN + MNAMELEN, "ffs");	/*  f_mntfromname  */
#endif
		break;

	case NETBSD_SYS_break:
		debug("[ break(0x%llx): TODO ]\n", (long long)arg0);
		/*  TODO  */
		break;

	case NETBSD_SYS_readlink:
		charbuf = get_userland_string(cpu, arg0);
		debug("[ readlink(\"%s\",0x%lli,%lli) ]\n",
		    charbuf, (long long)arg1, (long long)arg2);
		if (arg2 != 0 && arg2 < 50000) {
			unsigned char *buf2 = malloc(arg2);
			buf2[arg2-1] = '\0';
			result_low = readlink((char *)charbuf,
			    (char *)buf2, arg2 - 1);
			if ((int64_t)result_low < 0) {
				error_flag = 1;
				error_code = errno;
			} else
				store_string(cpu, arg1, (char *)buf2);
			free(buf2);
		}
		free(charbuf);
		break;

	case NETBSD_SYS_sync:
		debug("[ sync() ]\n");
		sync();
		break;

	case NETBSD_SYS_gettimeofday:
		debug("[ gettimeofday(0x%llx,0x%llx) ]\n",
		    (long long)arg0, (long long)arg1);
		result_low = gettimeofday(&tv, &tz);
		if (result_low) {
			error_flag = 1;
			error_code = errno;
		} else {
			if (arg0 != 0) {
				/*  Store tv.tv_sec and tv.tv_usec as
				    'long' (32-bit) values:  */
				store_32bit_word(cpu, arg0 + 0,
				    tv.tv_sec);
				store_32bit_word(cpu, arg0 + 4,
				    tv.tv_usec);
			}
			if (arg1 != 0) {
				/*  Store tz.tz_minuteswest and
				    tz.tz_dsttime as 'long'
				    (32-bit) values:  */
				store_32bit_word(cpu, arg1 + 0,
				    tz.tz_minuteswest);
				store_32bit_word(cpu, arg1 + 4,
				    tz.tz_dsttime);
			}
		}
		break;

	case NETBSD_SYS_mmap:
		debug("[ mmap(0x%x,%i,%i,%i,%i,0x%llx): TODO ]\n",
		    arg0, arg1, arg2, arg3, stack0, (long long)stack1);

		if ((int32_t)stack0 == -1) {
			/*
			 *  Anonymous allocation:
			 *
			 *  TODO:  Fix this!!!
			 *
			 *  This quick hack simply allocates anonymous
			 *  mmap memory approximately below the stack.
			 *  This will probably not work with dynamically
			 *  loaded libraries and such.
			 */
			static uint32_t mmap_anon_ptr = 0x70000000;
			mmap_anon_ptr -= arg1;
			/*  round down to page boundary:  */
			mmap_anon_ptr &= ~4095;
			debug("ANON: %i bytes at 0x%08x (TODO: not "
			    "working yet?)\n", (int)arg1,
			    mmap_anon_ptr);
			result_low = mmap_anon_ptr;
		} else {
			/*  Return NULL for now  */
		}
		break;

	case NETBSD_SYS_dup:
		debug("[ dup(%i) ]\n", (int)arg0);
		result_low = dup(arg0);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		break;

	case NETBSD_SYS_socket:
		debug("[ socket(%i,%i,%i) ]\n",
		    (int)arg0, (int)arg1, (int)arg2);
		result_low = socket(arg0,arg1,arg2);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		break;

	case NETBSD_SYS_issetugid:
		debug("[ issetugid() ]\n");
		/*  TODO: actually call the real issetugid?  */
		break;

	case NETBSD_SYS_nanosleep:
		debug("[ nanosleep(0x%llx,0x%llx) ]\n",
		    (long long)arg0, (long long)arg1);

		if (arg0 != 0) {
			uint32_t sec = load_32bit_word(cpu, arg0 + 0);
			uint32_t nsec = load_32bit_word(cpu, arg0 + 4);
			struct timespec ts;
			ts.tv_sec = sec;
			ts.tv_nsec = nsec;
			result_low = nanosleep(&ts, NULL);
			if (result_low)
				fprintf(stderr, "netbsd emulation "
				    "nanosleep() failed\n");
			/*  TODO: arg1  */
		} else {
			error_flag = 1;
			error_code = 14;  /*  EFAULT  */
		}
		break;

	case NETBSD_SYS___fstat13:
		debug("[ __fstat13(%lli,0x%llx): TODO ]\n",
		    (long long)arg0, (long long)arg1);
		error_flag = 1;
		error_code = 9;  /*  EBADF  */
		break;

	case NETBSD_SYS___getcwd:
		debug("[ __getcwd(0x%llx,%lli): TODO ]\n",
		    (long long)arg0, (long long)arg1);
		if (arg1 != 0 && arg1 < 500000) {
			char *buf = malloc(arg1);
			unsigned int i;

			getcwd(buf, arg1);

			/*  zero-terminate in host's space:  */
			buf[arg1 - 1] = 0;

			for (i = 0; i<arg1 && i < arg1; i++)
				cpu->memory_rw(cpu, cpu->mem, arg0 + i,
				    (unsigned char *)&buf[i], 1,
				    MEM_WRITE, CACHE_NONE);

			/*  zero-terminate in emulated space:  */
			cpu->memory_rw(cpu, cpu->mem, arg0 + arg1-1,
			    (unsigned char *)&buf[arg1 - 1],
			    1, MEM_WRITE, CACHE_NONE);

			free(buf);
		}
		result_low = arg0;
		break;

	case NETBSD_SYS___sigaction14:
		debug("[ __sigaction14(%lli,0x%llx,0x%llx): TODO ]\n",
		    (long long)arg0, (long long)arg1, (long long)arg2);
		error_flag = 1;
		error_code = 9;  /*  EBADF  */
		break;

	case NETBSD_SYS___sysctl:
		sysctl_name    = arg0;
		sysctl_namelen = arg1;
		sysctl_oldp    = arg2;
		sysctl_oldlenp = arg3;
		sysctl_newp    = load_32bit_word(cpu,
		    cpu->cd.mips.gpr[MIPS_GPR_SP]);
		    /*  TODO: +4 and +8 ??  */
		sysctl_newlen  = load_32bit_word(cpu,
		    cpu->cd.mips.gpr[MIPS_GPR_SP] + 4);
		debug("[ __sysctl(");

		name0 = load_32bit_word(cpu, sysctl_name + 0);
		name1 = load_32bit_word(cpu, sysctl_name + 4);
		name2 = load_32bit_word(cpu, sysctl_name + 8);
		name3 = load_32bit_word(cpu, sysctl_name + 12);
		debug("name (@ 0x%08x) = %i, %i, %i, %i) ]\n",
		    sysctl_name, name0, name1, name2, name3);

		if (name0 == CTL_KERN && name1 == KERN_HOSTNAME) {
			char hname[256];
			hname[0] = '\0';
			gethostname(hname, sizeof(hname));
			hname[sizeof(hname)-1] = '\0';
			if (sysctl_oldp != 0)
				store_string(cpu, sysctl_oldp, hname);
			if (sysctl_oldlenp != 0)
				store_32bit_word(cpu, sysctl_oldlenp,
				    strlen(hname));
		} else if (name0 == CTL_HW && name1 == HW_PAGESIZE) {
			if (sysctl_oldp != 0)
				store_32bit_word(cpu,
				    sysctl_oldp, 4096);
			if (sysctl_oldlenp != 0)
				store_32bit_word(cpu,
				    sysctl_oldlenp, sizeof(uint32_t));
		} else {
			error_flag = 1;
			error_code = 2;  /*  ENOENT  */
		}
		break;

	default:
		fatal("[ UNIMPLEMENTED netbsd syscall %i ]\n", sysnr);
		error_flag = 1;
		error_code = 78;  /*  ENOSYS  */
	}

	/*
	 *  NetBSD/mips return values:
	 *
	 *  a3 is 0 if the syscall was ok, otherwise 1.
	 *  v0 (and sometimes v1) contain the result value.
	 */
	cpu->cd.mips.gpr[MIPS_GPR_A3] = error_flag;
	if (error_flag)
		cpu->cd.mips.gpr[MIPS_GPR_V0] = error_code;
	else
		cpu->cd.mips.gpr[MIPS_GPR_V0] = result_low;

	if (result_high_set)
		cpu->cd.mips.gpr[MIPS_GPR_V1] = result_high;
}


/*
 *  useremul__ultrix():
 *
 *  Ultrix syscall emulation.
 */
static void useremul__ultrix(struct cpu *cpu, uint32_t code)
{
	int error_flag = 0, result_high_set = 0;
	uint64_t arg0,arg1,arg2,arg3,stack0,stack1,stack2;
	int sysnr = 0;
	uint64_t error_code = 0;
	uint64_t result_low = 0;
	uint64_t result_high = 0;
	struct timeval tv;
	struct timezone tz;
	int descr;
	uint64_t length, mipsbuf;
	unsigned char *charbuf;

	/*
	 *  Ultrix/pmax gets the syscall number in register v0,
	 *  and syscall arguments in registers a0, a1, ...
	 *
	 *  TODO:  If there is a __syscall-like syscall (as in NetBSD)
	 *  then 64-bit args may be passed in two registers or something...
	 *  If so, then copy from the section above (NetBSD).
	 */
	sysnr = cpu->cd.mips.gpr[MIPS_GPR_V0];

	arg0 = cpu->cd.mips.gpr[MIPS_GPR_A0];
	arg1 = cpu->cd.mips.gpr[MIPS_GPR_A1];
	arg2 = cpu->cd.mips.gpr[MIPS_GPR_A2];
	arg3 = cpu->cd.mips.gpr[MIPS_GPR_A3];
	/*  TODO:  stack arguments? Are these correct?  */
	stack0 = load_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_SP] + 0);
	stack1 = load_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_SP] + 4);
	stack2 = load_32bit_word(cpu, cpu->cd.mips.gpr[MIPS_GPR_SP] + 8);

	switch (sysnr) {

	case ULTRIX_SYS_exit:
		debug("[ exit(%i) ]\n", (int)arg0);
		cpu->running = 0;
		cpu->machine->exit_without_entering_debugger = 1;
		break;

	case ULTRIX_SYS_read:
		debug("[ read(%i,0x%llx,%lli) ]\n",
		    (int)arg0, (long long)arg1, (long long)arg2);

		if (arg2 != 0) {
			charbuf = malloc(arg2);
			if (charbuf == NULL) {
				fprintf(stderr, "out of memory in "
				    "useremul__ultrix()\n");
				exit(1);
			}

			result_low = read(arg0, charbuf, arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}

			/*  TODO: address validity check  */
			cpu->memory_rw(cpu, cpu->mem, arg1, charbuf,
			    arg2, MEM_WRITE, CACHE_DATA);

			free(charbuf);
		}
		break;

	case ULTRIX_SYS_write:
		descr   = arg0;
		mipsbuf = arg1;
		length  = arg2;
		debug("[ write(%i,0x%llx,%lli) ]\n",
		    (int)descr, (long long)mipsbuf, (long long)length);

		if (length != 0) {
			charbuf = malloc(length);
			if (charbuf == NULL) {
				fprintf(stderr, "out of memory in "
				    "useremul__ultrix()\n");
				exit(1);
			}

			/*  TODO: address validity check  */
			cpu->memory_rw(cpu, cpu->mem, mipsbuf, charbuf,
			    length, MEM_READ, CACHE_DATA);

			result_low = write(descr, charbuf, length);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			free(charbuf);
		}
		break;

	case ULTRIX_SYS_open:
		charbuf = get_userland_string(cpu, arg0);
		debug("[ open(\"%s\", 0x%llx, 0x%llx) ]\n",
		    charbuf, (long long)arg1, (long long)arg2);

		result_low = open((char *)charbuf, arg1, arg2);
		if ((int64_t)result_low < 0) {
			error_flag = 1;
			error_code = errno;
		}
		free(charbuf);
		break;

	case ULTRIX_SYS_close:
		descr = arg0;
		debug("[ close(%i) ]\n", (int)descr);

		/*  Special case because some Ultrix programs tend
		    to close low descriptors:  */
		if (descr <= 2) {
			error_flag = 1;
			error_code = 2;	/*  TODO: Ultrix ENOENT error code  */
			break;
		}

		error_code = close(descr);
		if (error_code != 0)
			error_flag = 1;
		break;

	case ULTRIX_SYS_break:
		debug("[ break(0x%llx): TODO ]\n", (long long)arg0);
		/*  TODO  */
		break;

	case ULTRIX_SYS_sync:
		debug("[ sync() ]\n");
		sync();
		break;

	case ULTRIX_SYS_getuid:
		debug("[ getuid() ]\n");
		result_low = getuid();
		break;

	case ULTRIX_SYS_getgid:
		debug("[ getgid() ]\n");
		result_low = getgid();
		break;

	case ULTRIX_SYS_dup:
		debug("[ dup(%i) ]\n", (int)arg0);
		result_low = dup(arg0);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		break;

	case ULTRIX_SYS_socket:
		debug("[ socket(%i,%i,%i) ]\n",
		    (int)arg0, (int)arg1, (int)arg2);
		result_low = socket(arg0,arg1,arg2);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		break;

	case ULTRIX_SYS_select:
		debug("[ select(%i,0x%x,0x%x,0x%x,0x%x): TODO ]\n",
		    (int)arg0, (int)arg1, (int)arg2, (int)arg3, (int)stack0);

		/*  TODO  */
		{
			fd_set fdset;
			FD_SET(3, &fdset);
			result_low = select(4, &fdset, NULL, NULL, NULL);
		}
		break;

	case ULTRIX_SYS_setsockopt:
		debug("[ setsockopt(%i,%i,%i,0x%x,%i): TODO ]\n",
		    (int)arg0, (int)arg1, (int)arg2, (int)arg3, (int)stack0);
		/*  TODO: len is not 4, len is stack0?  */
		charbuf = get_userland_buf(cpu, arg3, 4);
		/*  TODO: endianness of charbuf, etc  */
		result_low = setsockopt(arg0, arg1, arg2, (void *)charbuf, 4);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		free(charbuf);
		printf("setsockopt!!!! res = %i error=%i\n",
		    (int)result_low, (int)error_code);
		break;

	case ULTRIX_SYS_connect:
		debug("[ connect(%i,0x%x,%i) ]\n",
		    (int)arg0, (int)arg1, (int)arg2);
		charbuf = get_userland_buf(cpu, arg1, arg2);
		result_low = connect(arg0, (void *)charbuf, arg2);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		printf("connect!!!! res = %i error=%i\n",
		    (int)result_low, (int)error_code);
		free(charbuf);
		break;

	case ULTRIX_SYS_fcntl:
		debug("[ fcntl(%i,%i,0x%x): TODO ]\n",
		    (int)arg0, (int)arg1, (int)arg2);
		/*  TODO:  how about that third argument?  */
		result_low = fcntl(arg0, arg1, arg2);
		if ((int64_t)result_low < 0) {
			error_code = errno;
			error_flag = 1;
		}
		printf("fcntl!!!! res = %i error=%i\n",
		    (int)result_low, (int)error_code);
		break;

	case ULTRIX_SYS_stat43:
		charbuf = get_userland_string(cpu, arg0);
		debug("[ stat(\"%s\", 0x%llx): TODO ]\n",
		    charbuf, (long long)arg1);

		if (arg1 != 0) {
			struct stat st;
			result_low = stat((char *)charbuf, &st);
			if ((int64_t)result_low < 0) {
				error_flag = 1;
				error_code = errno;
			} else {
				/*  Fill in the Ultrix stat struct at arg1:  */

				/*  TODO  */
			}
		} else {
			error_flag = 1;
			error_code = 1111;	/*  TODO: ultrix ENOMEM?  */
		}
		free(charbuf);
		break;

	case ULTRIX_SYS_fstat:
		debug("[ fstat(%i, 0x%llx): TODO ]\n",
		    (int)arg0, (long long)arg1);

		if (arg1 != 0) {
			struct stat st;
			result_low = fstat(arg0, &st);
			if ((int64_t)result_low < 0) {
				error_flag = 1;
				error_code = errno;
			} else {
				/*  Fill in the Ultrix stat struct at arg1:  */

				/*  TODO  */
			}
		} else {
			error_flag = 1;
			error_code = 1111;	/*  TODO: ultrix ENOMEM?  */
		}
		break;

	case ULTRIX_SYS_getpagesize:
		debug("[ getpagesize() ]\n");
		result_low = 4096;
		break;

	case ULTRIX_SYS_getdtablesize:
		debug("[ getdtablesize() ]\n");
		result_low = getdtablesize();
		break;

	case ULTRIX_SYS_gethostname:
		debug("[ gethostname(0x%llx,%lli) ]\n",
		    (long long)arg0, (long long)arg1);
		result_low = 0;
		if (arg1 != 0 && arg1 < 500000) {
			unsigned char *buf = malloc(arg1);
			unsigned int i;

			result_low = gethostname((char *)buf, arg1);
			for (i = 0; i<arg1 && i < arg1; i++)
				cpu->memory_rw(cpu, cpu->mem, arg0 + i,
				    &buf[i], 1, MEM_WRITE, CACHE_NONE);

			free(buf);
		} else {
			error_flag = 1;
			error_code = 5555; /* TODO */  /*  ENOMEM  */
		}
		break;

	case ULTRIX_SYS_writev:
		descr = arg0;
		debug("[ writev(%lli,0x%llx,%lli) ]\n",
		    (long long)arg0, (long long)arg1, (long long)arg2);

		if (arg1 != 0) {
			unsigned int i, total = 0;

			for (i=0; i<arg2; i++) {
				uint32_t iov_base, iov_len;
				iov_base = load_32bit_word(cpu,
				    arg1 + 8*i + 0);	/*  char *  */
				iov_len  = load_32bit_word(cpu,
				    arg1 + 8*i + 4);	/*  size_t  */

				if (iov_len != 0) {
					unsigned char *charbuf =
					    malloc(iov_len);
					if (charbuf == NULL) {
						fprintf(stderr, "out of memory"
						    " in useremul__ultrix()\n");
						exit(1);
					}

					/*  TODO: address validity check  */
					cpu->memory_rw(cpu, cpu->mem, (uint64_t)
					    iov_base, charbuf, iov_len,
					    MEM_READ, CACHE_DATA);
					total += write(descr, charbuf, iov_len);
					free(charbuf);
				}
			}

			result_low = total;
		}
		break;

	case ULTRIX_SYS_gethostid:
		debug("[ gethostid() ]\n");
		/*  This is supposed to return a unique 32-bit host id.  */
		result_low = 0x12345678;
		break;

	case ULTRIX_SYS_gettimeofday:
		debug("[ gettimeofday(0x%llx,0x%llx) ]\n",
		    (long long)arg0, (long long)arg1);
		result_low = gettimeofday(&tv, &tz);
		if (result_low) {
			error_flag = 1;
			error_code = errno;
		} else {
			if (arg0 != 0) {
				/*  Store tv.tv_sec and tv.tv_usec
				    as 'long' (32-bit) values:  */
				store_32bit_word(cpu, arg0 + 0, tv.tv_sec);
				store_32bit_word(cpu, arg0 + 4, tv.tv_usec);
			}
			if (arg1 != 0) {
				/*  Store tz.tz_minuteswest and
				    tz.tz_dsttime as 'long' (32-bit) values:  */
				store_32bit_word(cpu, arg1 + 0,
				    tz.tz_minuteswest);
				store_32bit_word(cpu, arg1 + 4, tz.tz_dsttime);
			}
		}
		break;

	default:
		fatal("[ UNIMPLEMENTED ultrix syscall %i ]\n", sysnr);
		error_flag = 1;
		error_code = 78;  /*  ENOSYS  */
	}

	/*
	 *  Ultrix/mips return values:
	 *
	 *  TODO
	 *
	 *  a3 is 0 if the syscall was ok, otherwise 1.
	 *  v0 (and sometimes v1) contain the result value.
	 */
	cpu->cd.mips.gpr[MIPS_GPR_A3] = error_flag;
	if (error_flag)
		cpu->cd.mips.gpr[MIPS_GPR_V0] = error_code;
	else
		cpu->cd.mips.gpr[MIPS_GPR_V0] = result_low;

	if (result_high_set)
		cpu->cd.mips.gpr[MIPS_GPR_V1] = result_high;

	/* TODO */
}


/*
 *  useremul_name_to_useremul():
 *
 *  Example:
 *     Input:  name = "netbsd/pmax"
 *     Output: sets *arch = ARCH_MIPS, *machine_name = "NetBSD/pmax",
 *             and *cpu_name = "R3000".
 */
void useremul_name_to_useremul(struct cpu *cpu, char *name, int *arch,
	char **machine_name, char **cpu_name)
{
	struct syscall_emul *sep;

	sep = first_syscall_emul;

	while (sep != NULL) {
		if (strcasecmp(name, sep->name) == 0) {
			if (cpu_family_ptr_by_number(sep->arch) == NULL) {
				printf("\nSupport for the CPU family needed"
				    " for '%s' userland emulation was not"
				    " enabled at configuration time.\n",
				    sep->name);
				exit(1);
			}

			if (cpu != NULL)
				cpu->useremul_syscall = sep->f;

			if (arch != NULL)
				*arch = sep->arch;

			if (machine_name != NULL) {
				*machine_name = strdup(sep->name);
				if (*machine_name == NULL) {
					printf("out of memory\n");
					exit(1);
				}
			}

			if (cpu_name != NULL) {
				*cpu_name = strdup(sep->cpu_name);
				if (*cpu_name == NULL) {
					printf("out of memory\n");
					exit(1);
				}
			}
			return;
		}

		sep = sep->next;
	}

	fatal("Unknown userland emulation '%s'\n", name);
	exit(1);
}


/*
 *  add_useremul():
 *
 *  For internal use, from useremul_init() only. Adds an emulation mode.
 */
static void add_useremul(char *name, int arch, char *cpu_name,
	void (*f)(struct cpu *, uint32_t),
	void (*setup)(struct cpu *, int, char **))
{
	struct syscall_emul *sep;

	sep = malloc(sizeof(struct syscall_emul));
	if (sep == NULL) {
		printf("add_useremul(): out of memory\n");
		exit(1);
	}
	memset(sep, 0, sizeof(sep));

	sep->name     = name;
	sep->arch     = arch;
	sep->cpu_name = cpu_name;
	sep->f        = f;
	sep->setup    = setup;

	sep->next = first_syscall_emul;
	first_syscall_emul = sep;
}


/*
 *  useremul_list_emuls():
 *
 *  List all available userland emulation modes.  (Actually, only those which
 *  have CPU support enabled.)
 */
void useremul_list_emuls(void)
{
	struct syscall_emul *sep;
	int iadd = 8;

	sep = first_syscall_emul;

	if (sep == NULL)
		return;

	debug("The following userland-only (syscall) emulation modes are"
	    " available:\n\n");
	debug_indentation(iadd);

	while (sep != NULL) {
		if (cpu_family_ptr_by_number(sep->arch) != NULL) {
			debug("%s (default CPU \"%s\")\n",
			    sep->name, sep->cpu_name);
		}

		sep = sep->next;
	}

	debug_indentation(-iadd);
	debug("\n");
}


/*
 *  useremul_init():
 *
 *  This function should be called before any other useremul_*() function
 *  is used.
 */
void useremul_init(void)
{
	add_useremul("Linux/PPC64", ARCH_PPC, "PPC970",
	    useremul__linux, useremul__linux_setup);

	add_useremul("NetBSD/pmax", ARCH_MIPS, "R3000",
	    useremul__netbsd, useremul__netbsd_setup);

	add_useremul("NetBSD/powerpc", ARCH_PPC, "PPC750",
	    useremul__netbsd, useremul__netbsd_setup);

	add_useremul("Ultrix", ARCH_MIPS, "R3000",
	    useremul__ultrix, useremul__ultrix_setup);
}


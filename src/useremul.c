/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: useremul.c,v 1.2 2004-01-25 02:20:06 debug Exp $
 *
 *  Userland (syscall) emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "memory.h"
#include "misc.h"
#include "syscall_netbsd.h"
#include "sysctl_netbsd.h"

extern int errno;

extern int userland_emul;
extern char *last_filename;

/*  Max length of strings passed using syscall parameters:  */
#define	MAXLEN		8192


/*
 *  useremul_init():
 *
 *  Set up an emulated environment suitable for running
 *  userland code.  The program should already have been
 *  loaded into memory when this function is called.
 *
 *  TODO:  The only emulation so far is NetBSD/pmax, so
 *         when something more is added, this will have to
 *         be generalized.
 *
 *  TODO:  Most of this is just a quick hack to see if
 *         userland emulation works at all.
 */
void useremul_init(struct cpu *cpu, struct memory *mem)
{
	char *tmp_ptr;
	uint64_t stack_top = 0x7fff0000;
	uint64_t stacksize = 8 * 1048576;
	uint64_t stack_margin = 8192;
	uint64_t argc;
	uint64_t argv = stack_top - stack_margin + 64;
	uint64_t argv0 = stack_top - stack_margin + 128;
	uint64_t argv1 = stack_top - stack_margin + 512;

	/*  TODO: envp?  */

	switch (userland_emul) {
	case USERLAND_NETBSD_PMAX:
		/*  See netbsd/sys/src/arch/mips/mips_machdep.c:setregs()  */
		cpu->gpr[GPR_A0] = stack_top - stack_margin;
		cpu->gpr[25] = cpu->pc;		/*  reg. t9  */
		break;
	default:
		fprintf(stderr, "unknown userland emulation mode\n");
		exit(1);
	}

	/*  The userland stack:  */
	cpu->gpr[GPR_SP] = stack_top - stack_margin;
	add_symbol_name(stack_top - stacksize, stacksize, "userstack", 0);

	argc = 1;	/*  1 or 2  :-)  */

	/*  argc, argv, ..  */
	store_32bit_word(stack_top - stack_margin, argc);
	store_32bit_word(stack_top - stack_margin + 4, argv0);
	store_32bit_word(stack_top - stack_margin + 8, 0 /* argv1 */ );

	/*  Get argv[0] from last_filename:  */
	tmp_ptr = rindex(last_filename, '/');
	if (tmp_ptr == NULL)
		tmp_ptr = last_filename;
	else
		tmp_ptr ++;
	store_string(argv0, tmp_ptr);

	store_string(argv1, "15");
}


/*
 *  get_userland_string():
 *
 *  Warning: returns a pointer to a static array.
 */
unsigned char *get_userland_string(struct cpu *cpu, uint64_t baseaddr)
{
	static unsigned char charbuf[MAXLEN];
	int i;

	/*  TODO: address validity check  */

	for (i=0; i<MAXLEN; i++) {
		memory_rw(cpu, cpu->mem, baseaddr+i, charbuf+i, 1, MEM_READ, CACHE_DATA);
		if (charbuf[i] == '\0')
			break;
	}

	charbuf[MAXLEN-1] = 0;
	return charbuf;
}


/*
 *  useremul_syscall():
 *
 *  Handle userland syscalls.  This function is called whenever
 *  a userland process runs a 'syscall' instruction.  The code
 *  argument is the code embedded into the syscall instruction.
 *  (This 'code' value is not neccessarily used by specific
 *  emulations.)
 */
void useremul_syscall(struct cpu *cpu, uint32_t code)
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
	uint64_t length, mipsbuf, flags, i;
	unsigned char *charbuf;
	uint32_t sysctl_name, sysctl_namelen, sysctl_oldp, sysctl_oldlenp, sysctl_newp, sysctl_newlen;
	uint32_t name0, name1, name2, name3;

	switch (userland_emul) {
	case USERLAND_NETBSD_PMAX:
		sysnr = cpu->gpr[GPR_V0];

		if (sysnr == SYS___syscall) {
			sysnr = cpu->gpr[GPR_A0] + (cpu->gpr[GPR_A1] << 32);
			arg0 = cpu->gpr[GPR_A2];
			arg1 = cpu->gpr[GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			arg2 = load_32bit_word(cpu->gpr[GPR_SP] + 8);
			arg3 = load_32bit_word(cpu->gpr[GPR_SP] + 16);
			stack0 = load_32bit_word(cpu->gpr[GPR_SP] + 24);
			stack1 = load_32bit_word(cpu->gpr[GPR_SP] + 32);
			stack2 = load_32bit_word(cpu->gpr[GPR_SP] + 40);
		} else {
			arg0 = cpu->gpr[GPR_A0];
			arg1 = cpu->gpr[GPR_A1];
			arg2 = cpu->gpr[GPR_A2];
			arg3 = cpu->gpr[GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			stack0 = load_32bit_word(cpu->gpr[GPR_SP] + 4);
			stack1 = load_32bit_word(cpu->gpr[GPR_SP] + 8);
			stack2 = load_32bit_word(cpu->gpr[GPR_SP] + 12);
		}

		switch (sysnr) {

		case SYS_exit:
			debug("useremul_syscall(): netbsd exit()\n");
			cpu->running = 0;
			break;

		case SYS_write:
			descr   = arg0;
			mipsbuf = arg1;
			length  = arg2;
			debug("useremul_syscall(): netbsd write(%i,0x%llx,%lli)\n",
			    (int)descr, (long long)mipsbuf, (long long)length);

			if (length > 0) {
				charbuf = malloc(length);
				if (charbuf == NULL) {
					fprintf(stderr, "out of memory in useremul_syscall()\n");
					exit(1);
				}

				/*  TODO: address validity check  */
				memory_rw(cpu, cpu->mem, mipsbuf, charbuf, length, MEM_READ, CACHE_DATA);

				/*  TODO: file descriptors  */
				write(STDOUT_FILENO, charbuf, length);

				free(charbuf);
			}

			break;

		case SYS_open:
			charbuf = get_userland_string(cpu, arg0);
			debug("useremul_syscall(): netbsd open(\"%s\", 0x%llx)\n",
			    charbuf, cpu->gpr[GPR_A1]);

			error_flag = 1;
			error_code = 2;  /*  ENOENT  */
			break;

		case SYS_getfsstat:
			mipsbuf = arg0;
			length  = arg1;
			flags   = arg2;
			debug("useremul_syscall(): netbsd getfsstat(0x%llx,%lli,0x%llx)\n",
			    (long long)mipsbuf, (long long)length, (long long)flags);
			result_low = 0;		/*  nr of mounted filesystems, for now  (TODO)  */
			break;

		case SYS_break:
			debug("useremul_syscall(): netbsd break(0x%llx): TODO\n", (long long)arg0);
			error_flag = 1;
			error_code = 78;	/*  TODO  */
			break;

		case SYS_readlink:
			charbuf = get_userland_string(cpu, arg0);
			debug("useremul_syscall(): netbsd readlink(\"%s\",0x%lli,%lli)\n",
			    charbuf, (long long)arg1, (long long)arg2);
			if (arg2 > 0 && arg2 < 50000) {
				unsigned char buf2[arg2];
				buf2[arg2-1] = '\0';
				result_low = readlink(charbuf, buf2, sizeof(buf2)-1);
				if ((int64_t)result_low < 0) {
					error_flag = 1;
					error_code = errno;
				} else
					store_string(arg1, buf2);
			}
			break;

		case SYS_sync:
			debug("useremul_syscall(): netbsd sync()\n");
			sync();
			break;

		case SYS_gettimeofday:
			debug("useremul_syscall(): netbsd gettimeofday(0x%llx,0x%llx)\n",
			    (long long)arg0, (long long)arg1);
			result_low = gettimeofday(&tv, &tz);
			if (result_low) {
				error_flag = 1;
				error_code = errno;
			} else {
				if (arg0 != 0) {
					/*  Store tv.tv_sec and tv.tv_usec as 'long' (32-bit) values:  */
					store_32bit_word(arg0 + 0, tv.tv_sec);
					store_32bit_word(arg0 + 4, tv.tv_usec);
				}
				if (arg1 != 0) {
					/*  Store tz.tz_minuteswest and tz.tz_dsttime as 'long' (32-bit) values:  */
					store_32bit_word(arg1 + 0, tz.tz_minuteswest);
					store_32bit_word(arg1 + 4, tz.tz_dsttime);
				}
			}
			break;

		case SYS_mmap:
			debug("useremul_syscall(): netbsd mmap(0x%x,%i,%i,%i,%i,0x%llx): TODO\n",
			    arg0, arg1, arg2, arg3, stack0, (long long)stack1);
			/*  Return NULL for now  */
			break;

		case SYS_issetugid:
			debug("useremul_syscall(): netbsd issetugid()\n");
			/*  TODO: actually call the real issetugid?  */
			break;

		case SYS___fstat13:
			debug("useremul_syscall(): netbsd __fstat13(%lli,0x%llx)\n",
			    (long long)arg0, (long long)arg1);

			error_flag = 1;
			error_code = 9;  /*  EBADF  */
			break;

		case SYS___sysctl:
			sysctl_name    = arg0;
			sysctl_namelen = arg1;
			sysctl_oldp    = arg2;
			sysctl_oldlenp = arg3;
			sysctl_newp    = load_32bit_word(cpu->gpr[GPR_SP]);		/*  TODO: +4 and +8 ??  */
			sysctl_newlen  = load_32bit_word(cpu->gpr[GPR_SP] + 4);
			debug("useremul_syscall(): netbsd __sysctl(");

			name0 = load_32bit_word(sysctl_name + 0);
			name1 = load_32bit_word(sysctl_name + 4);
			name2 = load_32bit_word(sysctl_name + 8);
			name3 = load_32bit_word(sysctl_name + 12);
			debug("name (@ 0x%08x) = %i, %i, %i, %i)\n", sysctl_name,
			    name0, name1, name2, name3);

			if (name0 == CTL_KERN && name1 == KERN_HOSTNAME) {
				char hname[256];
				hname[0] = '\0';
				gethostname(hname, sizeof(hname));
				hname[sizeof(hname)-1] = '\0';
				if (sysctl_oldp != 0)
					store_string(sysctl_oldp, hname);
				if (sysctl_oldlenp != 0)
					store_32bit_word(sysctl_oldlenp, strlen(hname));
			} else if (name0 == CTL_HW && name1 == HW_PAGESIZE) {
				if (sysctl_oldp != 0)
					store_32bit_word(sysctl_oldp, 4096);
				if (sysctl_oldlenp != 0)
					store_32bit_word(sysctl_oldlenp, sizeof(uint32_t));
			} else {
				error_flag = 1;
				error_code = 2;  /*  ENOENT  */
			}

			break;

		default:
			fatal("UNIMPLEMENTED netbsd syscall %i\n", sysnr);
			error_flag = 1;
			error_code = 78;  /*  ENOSYS  */
		}

		/*
		 *  NetBSD/mips return values:
		 *
		 *  a3 is 0 if the syscall was ok, otherwise 1.
		 *  v0 (and sometimes v1) contain the result value.
		 */
		cpu->gpr[GPR_A3] = error_flag;
		if (error_flag)
			cpu->gpr[GPR_V0] = error_code;
		else
			cpu->gpr[GPR_V0] = result_low;
		if (result_high_set)
			cpu->gpr[GPR_V1] = result_high;
		break;
	default:
		fprintf(stderr, "useremul_syscall(): unimplemented syscall"
		   " emulation %i\n", userland_emul);
		exit(1);
	}
}


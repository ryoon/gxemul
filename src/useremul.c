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
 *  $Id: useremul.c,v 1.26 2005-01-19 15:39:10 debug Exp $
 *
 *  Userland (syscall) emulation.
 *
 *  TODO:
 *
 *	NetBSD/pmax:
 *		environment passing
 *		more syscalls
 *
 *	Other emulations?  Irix? Linux?
 *
 *	32-bit vs 64-bit problems? n32? o32?
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
 *  NOTE:  This module (useremul.c) is just a quick hack to see if
 *         userland emulation works at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "machine.h"
#include "misc.h"


#ifndef ENABLE_USERLAND


void useremul_init(struct cpu *cpu, int argc, char **host_argv)
{
}

void useremul_syscall(struct cpu *cpu, uint32_t code)
{
}


#else	/*  ENABLE_USERLAND  */


#include "memory.h"
#include "syscall_netbsd.h"
#include "sysctl_netbsd.h"
#include "syscall_ultrix.h"

/*  Max length of strings passed using syscall parameters:  */
#define	MAXLEN		8192


/*
 *  useremul_init():
 *
 *  Set up an emulated environment suitable for running
 *  userland code.  The program should already have been
 *  loaded into memory when this function is called.
 */
void useremul_init(struct cpu *cpu, int argc, char **host_argv)
{
	uint64_t stack_top = 0x7fff0000;
	uint64_t stacksize = 8 * 1048576;
	uint64_t stack_margin = 16384;
	uint64_t cur_argv;
	int i, i2;
	int envc = 1;

	switch (cpu->machine->userland_emul) {
	case USERLAND_NETBSD_PMAX:
		/*  See netbsd/sys/src/arch/mips/mips_machdep.c:setregs()  */
		cpu->gpr[GPR_A0] = stack_top - stack_margin;
		cpu->gpr[25] = cpu->pc;		/*  reg. t9  */
		break;
	case USERLAND_ULTRIX_PMAX:
		/*  TODO:  is this correct?  */
		cpu->gpr[GPR_A0] = stack_top - stack_margin;
		cpu->gpr[25] = cpu->pc;		/*  reg. t9  */
		break;
	default:
		fprintf(stderr, "unknown userland emulation mode\n");
		exit(1);
	}

	/*  The userland stack:  */
	cpu->gpr[GPR_SP] = stack_top - stack_margin;
	add_symbol_name(&cpu->machine->symbol_context,
	    stack_top - stacksize, stacksize, "userstack", 0);

	/*
	 *  Stack contents:  (TODO: emulation dependant?)
	 */
	store_32bit_word(cpu, stack_top - stack_margin, argc);

	cur_argv = stack_top - stack_margin + 128 + (argc + envc) * sizeof(uint32_t);
	for (i=0; i<argc; i++) {
		debug("adding argv[%i]: '%s'\n", i, host_argv[i]);

		store_32bit_word(cpu, stack_top - stack_margin + 4 + i*sizeof(uint32_t), cur_argv);
		store_string(cpu, cur_argv, host_argv[i]);
		cur_argv += strlen(host_argv[i]) + 1;
	}

	/*  Store a NULL value between the args and the environment strings:  */
	store_32bit_word(cpu, stack_top - stack_margin + 4 + i*sizeof(uint32_t), 0);  i++;

	/*  TODO: get environment strings from somewhere  */

	/*  Store all environment strings:  */
	for (i2 = 0; i2 < envc; i2 ++) {
		store_32bit_word(cpu, stack_top - stack_margin + 4 + (i+i2)*sizeof(uint32_t), cur_argv);
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
 *  Warning: returns a pointer to a static array.
 */
static unsigned char *get_userland_string(struct cpu *cpu, uint64_t baseaddr)
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
 *  get_userland_buf():
 *
 *  This can be used to retrieve buffers, for example inet_addr,
 *  from the emulated memory.
 *
 *  Warning: returns a pointer to a static array.
 *  TODO: combine this with get_userland_string() in some way
 */
static unsigned char *get_userland_buf(struct cpu *cpu,
	uint64_t baseaddr, int len)
{
	static unsigned char charbuf[MAXLEN];
	int i;

	if (len > MAXLEN) {
		fprintf(stderr, "get_userland_buf(): len is more than MAXLEN (%i > %i)\n",
		    len, MAXLEN);
		exit(1);
	}

	/*  TODO: address validity check  */
	for (i=0; i<len; i++) {
		memory_rw(cpu, cpu->mem, baseaddr+i, charbuf+i, 1, MEM_READ, CACHE_DATA);
		debug(" %02x", charbuf[i]);
	}
	debug("\n");

	return charbuf;
}


/*
 *  useremul_syscall():
 *
 *  Handle userland syscalls.  This function is called whenever
 *  a userland process runs a 'syscall' instruction.  The code
 *  argument is the code embedded into the syscall instruction.
 *  (This 'code' value is not necessarily used by specific
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
	uint64_t length, mipsbuf, flags;
	unsigned char *charbuf;
	uint32_t sysctl_name, sysctl_namelen, sysctl_oldp, sysctl_oldlenp, sysctl_newp, sysctl_newlen;
	uint32_t name0, name1, name2, name3;

	switch (cpu->machine->userland_emul) {
	case USERLAND_NETBSD_PMAX:
		sysnr = cpu->gpr[GPR_V0];

		if (sysnr == NETBSD_SYS___syscall) {
			sysnr = cpu->gpr[GPR_A0] + (cpu->gpr[GPR_A1] << 32);
			arg0 = cpu->gpr[GPR_A2];
			arg1 = cpu->gpr[GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			arg2 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 8);
			arg3 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 16);
			stack0 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 24);
			stack1 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 32);
			stack2 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 40);
		} else {
			arg0 = cpu->gpr[GPR_A0];
			arg1 = cpu->gpr[GPR_A1];
			arg2 = cpu->gpr[GPR_A2];
			arg3 = cpu->gpr[GPR_A3];
			/*  TODO:  stack arguments? Are these correct?  */
			stack0 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 4);
			stack1 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 8);
			stack2 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 12);
		}

		switch (sysnr) {

		case NETBSD_SYS_exit:
			debug("useremul_syscall(): netbsd exit()\n");
			cpu->running = 0;
			cpu->machine->exit_without_entering_debugger = 1;
			break;

		case NETBSD_SYS_read:
			debug("useremul_syscall(): netbsd read(%i,0x%llx,%lli)\n",
			    (int)arg0, (long long)arg1, (long long)arg2);

			if (arg2 != 0) {
				charbuf = malloc(arg2);
				if (charbuf == NULL) {
					fprintf(stderr, "out of memory in useremul_syscall()\n");
					exit(1);
				}

				result_low = read(arg0, charbuf, arg2);
				if ((int64_t)result_low < 0) {
					error_code = errno;
					error_flag = 1;
				}

				/*  TODO: address validity check  */
				memory_rw(cpu, cpu->mem, arg1, charbuf, arg2, MEM_WRITE, CACHE_DATA);

				free(charbuf);
			}
			break;

		case NETBSD_SYS_write:
			descr   = arg0;
			mipsbuf = arg1;
			length  = arg2;
			debug("useremul_syscall(): netbsd write(%i,0x%llx,%lli)\n",
			    (int)descr, (long long)mipsbuf, (long long)length);

			if (length != 0) {
				charbuf = malloc(length);
				if (charbuf == NULL) {
					fprintf(stderr, "out of memory in useremul_syscall()\n");
					exit(1);
				}

				/*  TODO: address validity check  */
				memory_rw(cpu, cpu->mem, mipsbuf, charbuf, length, MEM_READ, CACHE_DATA);

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
			debug("useremul_syscall(): netbsd open(\"%s\", 0x%llx, 0x%llx)\n",
			    charbuf, (long long)arg1, (long long)arg2);

			result_low = open((char *)charbuf, arg1, arg2);
			if ((int64_t)result_low < 0) {
				error_flag = 1;
				error_code = errno;
			}
			break;

		case NETBSD_SYS_close:
			descr   = arg0;
			debug("useremul_syscall(): netbsd close(%i)\n", (int)descr);

			error_code = close(descr);
			if (error_code != 0)
				error_flag = 1;
			break;

		case NETBSD_SYS_access:
			charbuf = get_userland_string(cpu, arg0);
			debug("useremul_syscall(): netbsd access(\"%s\", 0x%llx)\n",
			    charbuf, (long long) arg1);

			result_low = access((char *)charbuf, arg1);
			if (result_low != 0) {
				error_flag = 1;
				error_code = errno;
			}
			break;

		case NETBSD_SYS_getuid:
			debug("useremul_syscall(): netbsd getuid()\n");
			result_low = getuid();
			break;

		case NETBSD_SYS_geteuid:
			debug("useremul_syscall(): netbsd geteuid()\n");
			result_low = geteuid();
			break;

		case NETBSD_SYS_getgid:
			debug("useremul_syscall(): netbsd getgid()\n");
			result_low = getgid();
			break;

		case NETBSD_SYS_getegid:
			debug("useremul_syscall(): netbsd getegid()\n");
			result_low = getegid();
			break;

		case NETBSD_SYS_getfsstat:
			mipsbuf = arg0;
			length  = arg1;
			flags   = arg2;
			debug("useremul_syscall(): netbsd getfsstat(0x%llx,%lli,0x%llx)\n",
			    (long long)mipsbuf, (long long)length, (long long)flags);

			result_low = 0;		/*  nr of mounted filesystems, for now  (TODO)  */

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
			debug("useremul_syscall(): netbsd break(0x%llx): TODO\n", (long long)arg0);
			/*  TODO  */
			break;

		case NETBSD_SYS_readlink:
			charbuf = get_userland_string(cpu, arg0);
			debug("useremul_syscall(): netbsd readlink(\"%s\",0x%lli,%lli)\n",
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
			break;

		case NETBSD_SYS_sync:
			debug("useremul_syscall(): netbsd sync()\n");
			sync();
			break;

		case NETBSD_SYS_gettimeofday:
			debug("useremul_syscall(): netbsd gettimeofday(0x%llx,0x%llx)\n",
			    (long long)arg0, (long long)arg1);
			result_low = gettimeofday(&tv, &tz);
			if (result_low) {
				error_flag = 1;
				error_code = errno;
			} else {
				if (arg0 != 0) {
					/*  Store tv.tv_sec and tv.tv_usec as 'long' (32-bit) values:  */
					store_32bit_word(cpu, arg0 + 0, tv.tv_sec);
					store_32bit_word(cpu, arg0 + 4, tv.tv_usec);
				}
				if (arg1 != 0) {
					/*  Store tz.tz_minuteswest and tz.tz_dsttime as 'long' (32-bit) values:  */
					store_32bit_word(cpu, arg1 + 0, tz.tz_minuteswest);
					store_32bit_word(cpu, arg1 + 4, tz.tz_dsttime);
				}
			}
			break;

		case NETBSD_SYS_mmap:
			debug("useremul_syscall(): netbsd mmap(0x%x,%i,%i,%i,%i,0x%llx): TODO\n",
			    arg0, arg1, arg2, arg3, stack0, (long long)stack1);

			if ((int32_t)stack0 == -1) {
				/*
				 *  Anonymous allocation:
				 *
				 *  TODO:  Fix this!!!
				 *  This quick hack simply allocates anonymous
				 *  mmap memory approximately below the stack... :-!
				 *  This will probably not work with dynamically
				 *  loaded libraries and such.
				 */
				static uint32_t mmap_anon_ptr = 0x70000000;
				mmap_anon_ptr -= arg1;
				/*  round down to page boundary:  */
				mmap_anon_ptr &= ~4095;
				debug("ANON: %i bytes at 0x%08x (TODO: not working yet?)\n", (int)arg1, mmap_anon_ptr);
				result_low = mmap_anon_ptr;
			} else {
				/*  Return NULL for now  */
			}
			break;

		case NETBSD_SYS_dup:
			debug("useremul_syscall(): netbsd dup(%i)\n", (int)arg0);
			result_low = dup(arg0);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			break;

		case NETBSD_SYS_socket:
			debug("useremul_syscall(): netbsd socket(%i,%i,%i)\n", (int)arg0, (int)arg1, (int)arg2);
			result_low = socket(arg0,arg1,arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			break;

		case NETBSD_SYS_issetugid:
			debug("useremul_syscall(): netbsd issetugid()\n");
			/*  TODO: actually call the real issetugid?  */
			break;

		case NETBSD_SYS_nanosleep:
			debug("useremul_syscall(): netbsd nanosleep(0x%llx,0x%llx)\n",
			    (long long)arg0, (long long)arg1);

			if (arg0 != 0) {
				uint32_t sec = load_32bit_word(cpu, arg0 + 0);
				uint32_t nsec = load_32bit_word(cpu, arg0 + 4);
				struct timespec ts;
				ts.tv_sec = sec;
				ts.tv_nsec = nsec;
				result_low = nanosleep(&ts, NULL);
				if (result_low)
					fprintf(stderr, "netbsd emulation nanosleep() failed\n");
				/*  TODO: arg1  */
			} else {
				error_flag = 1;
				error_code = 14;  /*  EFAULT  */
			}
			break;

		case NETBSD_SYS___fstat13:
			debug("useremul_syscall(): netbsd __fstat13(%lli,0x%llx): TODO\n",
			    (long long)arg0, (long long)arg1);

			error_flag = 1;
			error_code = 9;  /*  EBADF  */
			break;

		case NETBSD_SYS___getcwd:
			debug("useremul_syscall(): netbsd __getcwd(0x%llx,%lli): TODO\n",
			    (long long)arg0, (long long)arg1);
			if (arg1 != 0 && arg1 < 500000) {
				char *buf = malloc(arg1);
				unsigned int i;

				getcwd(buf, arg1);

				/*  zero-terminate in host's space:  */
				buf[arg1 - 1] = 0;

				for (i = 0; i<arg1 && i < arg1; i++)
					memory_rw(cpu, cpu->mem, arg0 + i,
					    (unsigned char *)&buf[i], 1,
					    MEM_WRITE, CACHE_NONE);

				/*  zero-terminate in emulated space:  */
				memory_rw(cpu, cpu->mem, arg0 + arg1-1,
				    (unsigned char *)&buf[arg1 - 1],
				    1, MEM_WRITE, CACHE_NONE);

				free(buf);
			}
			result_low = arg0;
			break;

		case NETBSD_SYS___sigaction14:
			debug("useremul_syscall(): netbsd __sigaction14(%lli,0x%llx,0x%llx): TODO\n",
			    (long long)arg0, (long long)arg1, (long long)arg2);

			error_flag = 1;
			error_code = 9;  /*  EBADF  */
			break;

		case NETBSD_SYS___sysctl:
			sysctl_name    = arg0;
			sysctl_namelen = arg1;
			sysctl_oldp    = arg2;
			sysctl_oldlenp = arg3;
			sysctl_newp    = load_32bit_word(cpu, cpu->gpr[GPR_SP]);		/*  TODO: +4 and +8 ??  */
			sysctl_newlen  = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 4);
			debug("useremul_syscall(): netbsd __sysctl(");

			name0 = load_32bit_word(cpu, sysctl_name + 0);
			name1 = load_32bit_word(cpu, sysctl_name + 4);
			name2 = load_32bit_word(cpu, sysctl_name + 8);
			name3 = load_32bit_word(cpu, sysctl_name + 12);
			debug("name (@ 0x%08x) = %i, %i, %i, %i)\n", sysctl_name,
			    name0, name1, name2, name3);

			if (name0 == CTL_KERN && name1 == KERN_HOSTNAME) {
				char hname[256];
				hname[0] = '\0';
				gethostname(hname, sizeof(hname));
				hname[sizeof(hname)-1] = '\0';
				if (sysctl_oldp != 0)
					store_string(cpu, sysctl_oldp, hname);
				if (sysctl_oldlenp != 0)
					store_32bit_word(cpu, sysctl_oldlenp, strlen(hname));
			} else if (name0 == CTL_HW && name1 == HW_PAGESIZE) {
				if (sysctl_oldp != 0)
					store_32bit_word(cpu, sysctl_oldp, 4096);
				if (sysctl_oldlenp != 0)
					store_32bit_word(cpu, sysctl_oldlenp, sizeof(uint32_t));
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

	case USERLAND_ULTRIX_PMAX:
		/*
		 *  Ultrix/pmax gets the syscall number in register v0,
		 *  and syscall arguments in registers a0, a1, ...
		 *
		 *  TODO:  If there is a __syscall-like syscall (as in NetBSD)
		 *  then 64-bit args may be passed in two registers or something...
		 *  If so, then copy from the section above (NetBSD).
		 */
		sysnr = cpu->gpr[GPR_V0];

		arg0 = cpu->gpr[GPR_A0];
		arg1 = cpu->gpr[GPR_A1];
		arg2 = cpu->gpr[GPR_A2];
		arg3 = cpu->gpr[GPR_A3];
		/*  TODO:  stack arguments? Are these correct?  */
		stack0 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 0);
		stack1 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 4);
		stack2 = load_32bit_word(cpu, cpu->gpr[GPR_SP] + 8);

		switch (sysnr) {

		case ULTRIX_SYS_exit:
			debug("useremul_syscall(): ultrix exit()\n");
			cpu->running = 0;
			cpu->machine->exit_without_entering_debugger = 1;
			break;

		case ULTRIX_SYS_read:
			debug("useremul_syscall(): ultrix read(%i,0x%llx,%lli)\n",
			    (int)arg0, (long long)arg1, (long long)arg2);

			if (arg2 != 0) {
				charbuf = malloc(arg2);
				if (charbuf == NULL) {
					fprintf(stderr, "out of memory in useremul_syscall()\n");
					exit(1);
				}

				result_low = read(arg0, charbuf, arg2);
				if ((int64_t)result_low < 0) {
					error_code = errno;
					error_flag = 1;
				}

				/*  TODO: address validity check  */
				memory_rw(cpu, cpu->mem, arg1, charbuf, arg2, MEM_WRITE, CACHE_DATA);

				free(charbuf);
			}
			break;

		case ULTRIX_SYS_write:
			descr   = arg0;
			mipsbuf = arg1;
			length  = arg2;
			debug("useremul_syscall(): ultrix write(%i,0x%llx,%lli)\n",
			    (int)descr, (long long)mipsbuf, (long long)length);

			if (length != 0) {
				charbuf = malloc(length);
				if (charbuf == NULL) {
					fprintf(stderr, "out of memory in useremul_syscall()\n");
					exit(1);
				}

				/*  TODO: address validity check  */
				memory_rw(cpu, cpu->mem, mipsbuf, charbuf, length, MEM_READ, CACHE_DATA);

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
			debug("useremul_syscall(): ultrix open(\"%s\", 0x%llx, 0x%llx)\n",
			    charbuf, (long long)arg1, (long long)arg2);

			result_low = open((char *)charbuf, arg1, arg2);
			if ((int64_t)result_low < 0) {
				error_flag = 1;
				error_code = errno;
			}
			break;

		case ULTRIX_SYS_close:
			descr   = arg0;
			debug("useremul_syscall(): ultrix close(%i)\n", (int)descr);

			/*  Special case because some Ultrix programs tend to close low descriptors:  */
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
			debug("useremul_syscall(): ultrix break(0x%llx): TODO\n", (long long)arg0);
			/*  TODO  */
			break;

		case ULTRIX_SYS_sync:
			debug("useremul_syscall(): ultrix sync()\n");
			sync();
			break;

		case ULTRIX_SYS_getuid:
			debug("useremul_syscall(): ultrix getuid()\n");
			result_low = getuid();
			break;

		case ULTRIX_SYS_getgid:
			debug("useremul_syscall(): ultrix getgid()\n");
			result_low = getgid();
			break;

		case ULTRIX_SYS_dup:
			debug("useremul_syscall(): ultrix dup(%i)\n", (int)arg0);
			result_low = dup(arg0);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			break;

		case ULTRIX_SYS_socket:
			debug("useremul_syscall(): ultrix socket(%i,%i,%i)\n", (int)arg0, (int)arg1, (int)arg2);
			result_low = socket(arg0,arg1,arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
			break;

		case ULTRIX_SYS_select:
			debug("useremul_syscall(): ultrix select(%i,0x%x,0x%x,0x%x,0x%x): TODO\n",
			    (int)arg0, (int)arg1, (int)arg2, (int)arg3, (int)stack0);

			/*  TODO  */
{
fd_set fdset;
FD_SET(3, &fdset);
result_low = select(4, &fdset, NULL, NULL, NULL);
}
			break;

		case ULTRIX_SYS_setsockopt:
			debug("useremul_syscall(): ultrix setsockopt(%i,%i,%i,0x%x,%i): TODO\n",
			    (int)arg0, (int)arg1, (int)arg2, (int)arg3, (int)stack0);
			/*  TODO: len is not 4, len is stack0?  */
			charbuf = get_userland_buf(cpu, arg3, 4);
			/*  TODO: endianness of charbuf, etc  */
			result_low = setsockopt(arg0, arg1, arg2, (void *)charbuf, 4);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
printf("setsockopt!!!! res = %i error=%i\n", (int)result_low, (int)error_code);
			break;

		case ULTRIX_SYS_connect:
			debug("useremul_syscall(): ultrix connect(%i,0x%x,%i)\n",
			    (int)arg0, (int)arg1, (int)arg2);
			charbuf = get_userland_buf(cpu, arg1, arg2);
			result_low = connect(arg0, (void *)charbuf, arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
printf("connect!!!! res = %i error=%i\n", (int)result_low, (int)error_code);
			break;

		case ULTRIX_SYS_fcntl:
			debug("useremul_syscall(): ultrix fcntl(%i,%i,0x%x): TODO\n",
			    (int)arg0, (int)arg1, (int)arg2);
			/*  TODO:  how about that third argument?  */
			result_low = fcntl(arg0, arg1, arg2);
			if ((int64_t)result_low < 0) {
				error_code = errno;
				error_flag = 1;
			}
printf("fcntl!!!! res = %i error=%i\n", (int)result_low, (int)error_code);
			break;

		case ULTRIX_SYS_stat43:
			charbuf = get_userland_string(cpu, arg0);
			debug("useremul_syscall(): ultrix stat(\"%s\", 0x%llx): TODO\n",
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
			break;

		case ULTRIX_SYS_fstat:
			debug("useremul_syscall(): ultrix fstat(%i, 0x%llx): TODO\n",
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
			debug("useremul_syscall(): ultrix getpagesize()\n");
			result_low = 4096;
			break;

		case ULTRIX_SYS_getdtablesize:
			debug("useremul_syscall(): ultrix getdtablesize()\n");
			result_low = getdtablesize();
			break;

		case ULTRIX_SYS_gethostname:
			debug("useremul_syscall(): ultrix gethostname(0x%llx,%lli)\n",
			    (long long)arg0, (long long)arg1);
			result_low = 0;
			if (arg1 != 0 && arg1 < 500000) {
				unsigned char *buf = malloc(arg1);
				unsigned int i;

				result_low = gethostname((char *)buf, arg1);

				for (i = 0; i<arg1 && i < arg1; i++)
					memory_rw(cpu, cpu->mem, arg0 + i, &buf[i], 1, MEM_WRITE, CACHE_NONE);

				free(buf);
			} else {
				error_flag = 1;
				error_code = 5555; /* TODO */  /*  ENOMEM  */
			}
			break;

		case ULTRIX_SYS_writev:
			descr = arg0;
			debug("useremul_syscall(): ultrix writev(%lli,0x%llx,%lli)\n",
			    (long long)arg0, (long long)arg1, (long long)arg2);

			if (arg1 != 0) {
				unsigned int i, total = 0;

				for (i=0; i<arg2; i++) {
					uint32_t iov_base, iov_len;
					iov_base = load_32bit_word(cpu, arg1 + 8*i + 0);	/*  char *  */
					iov_len  = load_32bit_word(cpu, arg1 + 8*i + 4);	/*  size_t  */

					if (iov_len != 0) {
						unsigned char *charbuf = malloc(iov_len);
						if (charbuf == NULL) {
							fprintf(stderr, "out of memory in useremul_syscall()\n");
							exit(1);
						}

						/*  TODO: address validity check  */
						memory_rw(cpu, cpu->mem, (uint64_t)iov_base, charbuf, iov_len, MEM_READ, CACHE_DATA);
						total += write(descr, charbuf, iov_len);
						free(charbuf);
					}
				}

				result_low = total;
			}
			break;

		case ULTRIX_SYS_gethostid:
			debug("useremul_syscall(): ultrix gethostid()\n");
			/*  This is supposed to return a unique 32-bit host id.  */
			result_low = 0x12345678;
			break;

		case ULTRIX_SYS_gettimeofday:
			debug("useremul_syscall(): ultrix gettimeofday(0x%llx,0x%llx)\n",
			    (long long)arg0, (long long)arg1);
			result_low = gettimeofday(&tv, &tz);
			if (result_low) {
				error_flag = 1;
				error_code = errno;
			} else {
				if (arg0 != 0) {
					/*  Store tv.tv_sec and tv.tv_usec as 'long' (32-bit) values:  */
					store_32bit_word(cpu, arg0 + 0, tv.tv_sec);
					store_32bit_word(cpu, arg0 + 4, tv.tv_usec);
				}
				if (arg1 != 0) {
					/*  Store tz.tz_minuteswest and tz.tz_dsttime as 'long' (32-bit) values:  */
					store_32bit_word(cpu, arg1 + 0, tz.tz_minuteswest);
					store_32bit_word(cpu, arg1 + 4, tz.tz_dsttime);
				}
			}
			break;

		default:
			fatal("UNIMPLEMENTED ultrix syscall %i\n", sysnr);
			error_flag = 1;
			error_code = 78;  /*  ENOSYS  */
		}

		/*
		 *  Ultrix/mips return values:
		 *
TODO
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
/* TODO */
		break;

	default:
		fprintf(stderr, "useremul_syscall(): unimplemented syscall"
		   " emulation %i\n", cpu->machine->userland_emul);
		exit(1);
	}
}


#endif	/*  ENABLE_USERLAND  */

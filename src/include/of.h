#ifndef	OF_H
#define	OF_H

/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: of.h,v 1.1 2005-11-27 03:47:05 debug Exp $
 *
 *  OpenFirmware emulation. (See src/promemul/of.c for details.)
 */

#ifdef OF_C

struct machine;
struct cpu;

#define	OF_N_MAX_ARGS		10
#define	OF_ARG_MAX_LEN		4096

struct of_device_property {
	struct of_device_property *next;
	char			*name;
	unsigned char		*data;
	uint32_t		len;
};

struct of_device {
	struct of_device	*next;
	int			handle;
	int			parent;
	char			*name;
	struct of_device_property *properties;
};

#define	OF_FIND(ptr,cond)	for (; ptr != NULL; ptr = ptr->next)	\
					if (cond)			\
						break;

#define	OF_SERVICE_ARGS		struct cpu *, char **, uint64_t, uint64_t

/*
 *  OpenFirmare service 'name' is defined by a OF_SERVICE(name). The code
 *  implementing the service should use OF_GET_ARG to read numerical arguments
 *  and use arg[] for strings.
 */
#define OF_SERVICE(n)		static int of__ ## n (struct cpu *cpu, \
				char **arg, uint64_t base, uint64_t retofs)
#define	OF_GET_ARG(i)		load_32bit_word(cpu, base + 12 + \
				sizeof(uint32_t) * (i))

struct of_service {
	struct of_service	*next;
	char			*name;
	int			(*f)(OF_SERVICE_ARGS);
	int			n_args;
	int			n_ret_args;
};

struct of_data {
	struct of_device	*of_devices;
	struct of_service	*of_services;
};

#endif

struct of_data *of_emul_init(struct machine *machine);
int of_emul(struct cpu *cpu);

#endif	/*  OF_H  */

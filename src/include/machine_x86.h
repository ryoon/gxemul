#ifndef	MACHINE_X86_H
#define	MACHINE_X86_H

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
 *
 *  $Id: machine_x86.h,v 1.4 2005-05-16 00:18:41 debug Exp $
 */

#include "misc.h"


/*
 *  Machine-specific data for x86 machines. (BIOS settings, etc.)
 */

struct pc_bios_disk {
	struct pc_bios_disk *next;

	int	nr;		/*  0x00 = A:, 0x80 = C:  */

	int	type;
	int	id;

	int	cylinders;
	int	heads;
	int	sectorspertrack;
};

#define	PC_BIOS_KBD_BUF_SIZE		256

struct machine_pc {
	int	initialized;
	int	curcolor;

	uint8_t	kbd_buf_scancode[PC_BIOS_KBD_BUF_SIZE];
	uint8_t	kbd_buf[PC_BIOS_KBD_BUF_SIZE];
	int	kbd_buf_head;
	int	kbd_buf_tail;

	struct pc_bios_disk *first_disk;
	struct pic8259_data *pic1;
	struct pic8259_data *pic2;
};


#endif	/*  MACHINE_X86_H  */

#ifndef	OPCODES_PPC_H
#define	OPCODES_PPC_H

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
 *  $Id: opcodes_ppc.h,v 1.4 2005-02-13 20:52:57 debug Exp $
 *
 *
 *  PPC and POWER opcodes.
 *
 *  Note: The define uses the PPC name, not the POWER name, when they differ.
 */

#define	PPC_HI6_MULLI		0x07
#define	PPC_HI6_SUBFIC		0x08

#define	PPC_HI6_ADDIC		0x0c
#define	PPC_HI6_ADDIC_DOT	0x0d
#define	PPC_HI6_ADDI		0x0e
#define	PPC_HI6_ADDIS		0x0f

#define	PPC_HI6_SC		0x11

#define	PPC_HI6_19		0x13
#define	  PPC_19_ISYNC		  150

#define	PPC_HI6_ORI		0x18
#define	PPC_HI6_ORIS		0x19
#define	PPC_HI6_XORI		0x1a
#define	PPC_HI6_XORIS		0x1b
#define	PPC_HI6_ANDI_DOT	0x1c
#define	PPC_HI6_ANDIS_DOT	0x1d
#define	PPC_HI6_30		0x1e
#define	  PPC_30_RLDICL		  0x0
#define	  PPC_30_RLDICR		  0x1
#define	PPC_HI6_31		0x1f
#define	  PPC_31_MTMSR		  146
#define	  PPC_31_SYNC		  598

#endif	/*  OPCODES_PPH_H  */

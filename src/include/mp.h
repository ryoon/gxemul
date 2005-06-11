#ifndef	MP_H
#define	MP_H

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
 *  $Id: mp.h,v 1.6 2005-06-11 11:53:37 debug Exp $
 *
 *  dev_mp definitions.
 */

#define	MIPS_IPI_INT			6

#define	DEV_MP_ADDRESS			0x0000000011000000ULL
#define	DEV_MP_LENGTH			0x0000000000000100ULL
#define     DEV_MP_WHOAMI		    0x0000
#define     DEV_MP_NCPUS		    0x0010
#define     DEV_MP_STARTUPCPU		    0x0020
#define     DEV_MP_STARTUPADDR		    0x0030
#define     DEV_MP_PAUSE_ADDR		    0x0040
#define     DEV_MP_PAUSE_CPU		    0x0050
#define     DEV_MP_UNPAUSE_CPU		    0x0060
#define     DEV_MP_STARTUPSTACK		    0x0070
#define     DEV_MP_HARDWARE_RANDOM	    0x0080
#define     DEV_MP_MEMORY		    0x0090
#define	    DEV_MP_IPI_ONE		    0x00a0
#define	    DEV_MP_IPI_MANY		    0x00b0
#define	    DEV_MP_IPI_READ		    0x00c0

#endif	/*  MP_H  */

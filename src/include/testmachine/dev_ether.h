#ifndef	TESTMACHINE_ETHER_H
#define	TESTMACHINE_ETHER_H

/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_ether.h,v 1.1 2006-06-12 10:21:12 debug Exp $
 *
 *  Definitions used by the "ether" device in GXemul.
 */


#define	DEV_ETHER_ADDRESS		0x14000000
#define	DEV_ETHER_LENGTH		0x8000

#define	    DEV_ETHER_BUFFER		    0x0000
#define	    DEV_ETHER_BUFFER_SIZE	    0x4000
#define	    DEV_ETHER_STATUS		    0x4000
#define	    DEV_ETHER_PACKETLENGTH	    0x4010
#define	    DEV_ETHER_COMMAND		    0x4020

/*  Status bits:  */
#define	DEV_ETHER_STATUS_PACKET_RECEIVED		1
#define	DEV_ETHER_STATUS_MORE_PACKETS_AVAILABLE		2

/*  Commands:  */
#define	DEV_ETHER_COMMAND_RX		0
#define	DEV_ETHER_COMMAND_TX		1


#endif	/*  TESTMACHINE_ETHER_H  */

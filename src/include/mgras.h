/*  mips64emul: $Id: mgras.h,v 1.1 2004-07-17 19:13:24 debug Exp $  */
/*
 *  Copyright (C) 2004 by Stanislaw Skowronek.  All rights reserved.
 *
 *  This source file is licensed under two licenses. User is allowed to
 *  choose one of them.
 *
 *  1. BSD-STYLE LICENSE
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
 *  2. GNU PUBLIC LICENSE
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Mardi Gras in Speedracer / IMPACTSR / Octane Impact include file.
 *
 *  Motto: we live and learn...
 */
#ifndef _MGRAS_H
#define _MGRAS_H

#define MGRAS_BASE		0x900000001C000000

/* raster engine - RE4/PP1 */

#define MGRAS_PIPE		(*((volatile unsigned long *)(MGRAS_BASE+0x020400)))

#define MGRAS_SET_COLOR		0x00185C0400000000
#define MGRAS_SET_STARTXY	0x0018460400000000
#define MGRAS_SET_STOPXY	0x0018470400000000
#define MGRAS_DRAW_RECT		0x0019100400018000
#define MGRAS_DRAW_BITMAP	0x0019100400418008
#define MGRAS_SEND_CMD		0x001C130400000018
#define MGRAS_SEND_LINE		0x001C700400000000

#define MGRAS_RE_BUS_SYNC	(*((volatile unsigned int *)(MGRAS_BASE+0x05821c)))
#define MGRAS_RE_STATUS		(*((volatile unsigned int *)(MGRAS_BASE+0x02c578)))

/* display generator - VC3 */

#define MGRAS_WAIT_RT		(*((volatile unsigned int *)(MGRAS_BASE+0x020100)))
#define MGRAS_WAIT_PAL		(*((volatile unsigned char *)(MGRAS_BASE+0x071208)))
#define MGRAS_PAL_INDEX		(*((volatile unsigned short *)(MGRAS_BASE+0x070C30)))
#define MGRAS_PAL_MAGIC1	(*((volatile unsigned char *)(MGRAS_BASE+0x071C08)))
#define MGRAS_PAL_MAGIC2	(*((volatile unsigned char *)(MGRAS_BASE+0x071C88)))
#define MGRAS_PAL_MODE		(*((volatile unsigned int *)(MGRAS_BASE+0x071F00)))
#define MGRAS_PAL_DATA		(*((volatile unsigned int *)(MGRAS_BASE+0x070D18)))
#define MGRAS_CURSOR_POS	(*((volatile unsigned int *)(MGRAS_BASE+0x072038)))

/* geometry engine - GE11 */

#define MGRAS_GE11_DATA_A	(*((volatile unsigned int *)(MGRAS_BASE+0x058040)))
#define MGRAS_GE11_ADDR_A	(*((volatile unsigned int *)(MGRAS_BASE+0x058044)))
#define MGRAS_GE11_DATA_B	(*((volatile unsigned int *)(MGRAS_BASE+0x058048)))
#define MGRAS_GE11_ADDR_B	(*((volatile unsigned int *)(MGRAS_BASE+0x05804c)))

#define MGRAS_GE11_DIAG_CTRL	(*((volatile unsigned int *)(MGRAS_BASE+0x072408)))
#define MGRAS_GE11_DIAG_READ	(*((volatile unsigned int *)(MGRAS_BASE+0x05822c)))
#define MGRAS_GE11_DIAG_ACK	(*((volatile unsigned int *)(MGRAS_BASE+0x058230)))

#define MGRAS_GE11_UCODE_BASE	0x200000
#define MGRAS_GE11_UCODE_EXEC	0x40000
#define MGRAS_GE11_UCODE_DIAG	0x40010
#define MGRAS_GE11_UCODE_COTM	0x40018
#define MGRAS_GE11_UCODE_MODE	0x40038
#define MGRAS_GE11_MODE_NORMAL	0xd0
#define MGRAS_GE11_MODE_DIAG	0xd1
#define MGRAS_GE11_DIAG_REGSEL	0x80

#define MGRAS_GE11_PIPE_D	(*((volatile unsigned long *)(MGRAS_BASE+0x124400)))
#define MGRAS_GE11_PIPE_W	(*((volatile unsigned int *)(MGRAS_BASE+0x124400)))
#define MGRAS_GE11_PIPE_H	(*((volatile unsigned short *)(MGRAS_BASE+0x124400)))
#define MGRAS_GE11_PIPE_B	(*((volatile unsigned char *)(MGRAS_BASE+0x124400)))

/* bus controller - HQ3 */

#define MGRAS_HQ3_WINDOW	(*((volatile unsigned int *)(MGRAS_BASE+0x008000)))
#define MGRAS_HQ3_CONFIG	(*((volatile unsigned int *)(MGRAS_BASE+0x011000)))
#define MGRAS_HQ3_GIOCONF	(*((volatile unsigned int *)(MGRAS_BASE+0x011008)))
#define MGRAS_HQ3_STATUS	(*((volatile unsigned int *)(MGRAS_BASE+0x020000)))
#define MGRAS_HQ3_FIFOSTATUS	(*((volatile unsigned int *)(MGRAS_BASE+0x020008)))
#define MGRAS_HQ3_FLAGSET	(*((volatile unsigned int *)(MGRAS_BASE+0x020010)))
#define MGRAS_HQ3_DMABUSY	(*((volatile unsigned int *)(MGRAS_BASE+0x020200)))
#define MGRAS_HQ3_UCODE		((volatile unsigned int *)(MGRAS_BASE+0x050000))
#define MGRAS_HQ3_PC		(*(volatile unsigned int *)(MGRAS_BASE+0x056000))

#endif

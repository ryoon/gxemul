/*  GXemul: $Id: dreamcast_pvr.h,v 1.2 2006-10-21 02:39:08 debug Exp $  */
/*	$NetBSD: pvr.c,v 1.22 2006/04/12 19:38:22 jmmv Exp $	*/

#ifndef	DREAMCAST_PVR_H
#define	DREAMCAST_PVR_H

/*
 *  Note: This was pvr.c in NetBSD. It has been extended with reasonably
 *  similar symbolnames from http://www.ludd.luth.se/~jlo/dc/powervr-reg.txt.
 *
 *  There are still many things missing.
 */

/*-
 * Copyright (c) 2001 Marcus Comstedt.
 * Copyright (c) 2001 Jason R. Thorpe.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Marcus Comstedt.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1998, 1999 Tohru Nishimura.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Tohru Nishimura
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define	PVRREG_FBSTART		0x05000000
#define	PVRREG_REGSTART		0x005f8000

#define	PVRREG_REGSIZE		0x00002000


#define	PVRREG_ID		0x00

#define	PVRREG_REVISION		0x04
#define	PVR_REVISION_MINOR_MASK	0xf
#define	PVR_REVISION_MAJOR_MASK	0xf0
#define	PVR_REVISION_MAJOR_SHIFT	4

#define	PVRREG_RESET		0x08
#define	PVR_RESET_TA		0x00000001
#define	PVR_RESET_PVR		0x00000002
#define	PVR_RESET_BUS		0x00000004

#define	PVRREG_STARTRENDER	0x14

#define	PVRREG_OB_ADDR		0x20
/*  Object Buffer start address. Bits 0..19 should always be zero.  */
#define	PVR_OB_ADDR_MASK	0x00f00000

#define	PVRREG_TILEBUF_ADDR	0x2c
#define	PVR_TILEBUF_ADDR_MASK	0x00fffff8

#define	PVRREG_SPANSORT		0x30
#define	PVR_SPANSORT_SPAN0		0x00000001
#define	PVR_SPANSORT_SPAN1		0x00000100
#define	PVR_SPANSORT_TSP_CACHE_ENABLE	0x00010000

#define	PVRREG_BRDCOLR		0x40
#define	BRDCOLR_BLUE(x)		((x) << 0)
#define	BRDCOLR_GREEN(x)	((x) << 8)
#define	BRDCOLR_RED(x)		((x) << 16)

#define	PVRREG_DIWMODE		0x44
#define	DIWMODE_DE		(1U << 0)	/* display enable */
#define	DIWMODE_SD		(1U << 1)	/* scan double enable */
#define	DIWMODE_COL(x)		((x) << 2)
#define	DIWMODE_COL_RGB555	DIWMODE_COL(0)	/* RGB555, 16-bit */
#define	DIWMODE_COL_RGB565	DIWMODE_COL(1)	/* RGB565, 16-bit */
#define	DIWMODE_COL_RGB888	DIWMODE_COL(2)	/* RGB888, 24-bit */
#define	DIWMODE_COL_ARGB888	DIWMODE_COL(3)	/* RGB888, 32-bit */
#define	DIWMODE_C		(1U << 23)	/* 2x clock enable (VGA) */
#define	DIWMODE_DE_MASK		0x00000001
#define	DIWMODE_SD_MASK		0x00000002	/*  Line double  */
#define	DIWMODE_COL_MASK	0x0000000c	/*  Pixel mode  */
#define	DIWMODE_COL_SHIFT	2
#define	DIWMODE_EX_MASK		0x00000070	/*  Extend bits  */
#define	DIWMODE_EX_SHIFT	4
#define	DIWMODE_TH_MASK		0x0000ff00	/*  ARGB8888 threshold  */
#define	DIWMODE_TH_SHIFT	8
#define	DIWMODE_SL_MASK		0x003f0000	/*  Strip Length  */
#define	DIWMODE_SL_SHIFT	16
#define	DIWMODE_SE_MASK		0x00400000	/*  Strip Buffer enabled  */
#define	DIWMODE_C_MASK		0x00800000	/*  Clock double  */

#define	PVRREG_FB_RENDER_CFG	0x48
/*  TODO  */

#define	PVRREG_DIWADDRL		0x50

#define	PVRREG_DIWADDRS		0x54

#define	PVRREG_DIWSIZE		0x5c
#define	DIWSIZE_DPL(x)		((x) << 0)	/* pixel data per line */
#define	DIWSIZE_LPF(x)		((x) << 10)	/* lines per field */
#define	DIWSIZE_MODULO(x)	((x) << 20)	/* words to skip + 1 */
#define	DIWSIZE_MASK		0x3ff		/*  All fields are 10 bits.  */
#define	DIWSIZE_DPL_SHIFT	0
#define	DIWSIZE_LPF_SHIFT	10
#define	DIWSIZE_MODULO_SHIFT	20

#define	PVRREG_RASEVTPOS	0xcc
#define	RASEVTPOS_BOTTOM(x)	((x) << 0)
#define	RASEVTPOS_TOP(x)	((x) << 16)

#define	PVRREG_SYNCCONF		0xd0
#define	SYNCCONF_VP		(1U << 0)	/* V-sync polarity */
#define	SYNCCONF_HP		(1U << 1)	/* H-sync polarity */
#define	SYNCCONF_I		(1U << 4)	/* interlace */
#define	SYNCCONF_BC(x)		(1U << 6)	/* broadcast standard */
#define	SYNCCONF_VO		(1U << 8)	/* video output enable */
#define	SYNCCONF_VO_MASK	0x00000100
#define	SYNCCONF_BC_MASK	0x000000c0
#define	SYNCCONF_BC_SHIFT	6
#define	SYNCCONF_BC_VGA		  0
#define	SYNCCONF_BC_NTSC	  1
#define	SYNCCONF_BC_PAL		  2
#define	SYNCCONF_I_MASK		0x00000010
#define	SYNCCONF_HP_MASK	0x00000004	/*  Positive H-sync  */
#define	SYNCCONF_VP_MASK	0x00000002	/*  Positive V-sync  */

#define	PVRREG_BRDHORZ		0xd4
#define	BRDHORZ_STOP(x)		((x) << 0)
#define	BRDHORZ_START(x)	((x) << 16)

#define	PVRREG_SYNCSIZE		0xd8
#define	SYNCSIZE_H(x)		((x) << 0)
#define	SYNCSIZE_V(x)		((x) << 16)

#define	PVRREG_BRDVERT		0xdc
#define	BRDVERT_STOP(x)		((x) << 0)
#define	BRDVERT_START(x)	((x) << 16)

#define	PVRREG_DIWCONF		0xe8
#define	DIWCONF_LR		(1U << 8)	/* low-res */
#define	DIWCONF_MAGIC		(22 << 16)

#define	PVRREG_DIWHSTRT		0xec

#define	PVRREG_DIWVSTRT		0xf0
#define	DIWVSTRT_V1(x)		((x) << 0)
#define	DIWVSTRT_V2(x)		((x) << 16)

#define	PVRREG_PALETTE_CFG	0x108
#define	PVR_PALETTE_CFG_MODE_MASK	0x3
#define	PVR_PALETTE_CFG_MODE_ARGB1555	 0x0
#define	PVR_PALETTE_CFG_MODE_RGB565	 0x1
#define	PVR_PALETTE_CFG_MODE_ARGB4444	 0x2
#define	PVR_PALETTE_CFG_MODE_ARGB8888	 0x3

#define	PVRREG_SYNC_STAT	0x10c
#define	PVR_SYNC_STAT_VPOS_MASK			0x000003ff
#define	PVR_SYNC_STAT_INTERLACE_FIELD_EVEN	0x00000400
#define	PVR_SYNC_STAT_HBLANK			0x00001000
#define	PVR_SYNC_STAT_VBLANK			0x00002000

#define	PVRREG_TA_OPB_CFG	0x140
/*  TODO  */

#define	PVRREG_TA_INIT		0x144
#define	PVR_TA_INIT		0x80000000

#define	PVRREG_YUV_ADDR		0x148
#define	PVR_YUV_ADDR_MASK	0x00ffffe0

#define	PVRREG_YUV_CFG1		0x14c
/*  TODO  */

#define	PVRREG_YUV_STAT		0x150
/*  Nr of currently converted 16x16 macro blocks.  */
#define	PVR_YUV_STAT_BLOCKS_MASK 0x1fff

#define	PVRREG_TA_OPL_REINIT	0x160
#define	PVR_TA_OPL_REINIT	0x80000000

#define	PVRREG_TA_OPL_INIT	0x164
/*  Start of Object Pointer List allocation in VRAM.  */
#define	PVR_TA_OPL_INIT_MASK	0x00ffff80

#define	PVRREG_FOG_TABLE	0x0200
#define	PVR_FOG_TABLE_SIZE	0x0200

#define	PVRREG_PALETTE		0x1000
#define	PVR_PALETTE_SIZE	0x1000

#endif	/*  DREAMCAST_PVR_H  */


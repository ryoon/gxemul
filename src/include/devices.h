#ifndef	DEVICES_H
#define	DEVICES_H

/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: devices.h,v 1.9 2003-12-30 03:06:55 debug Exp $
 *
 *  Memory mapped devices:
 */

#include <inttypes.h>
#include <sys/types.h>

/* #ifdef WITH_X11
#include <X11/Xlib.h>
#endif */


/*  dev_asc.c:  */
#define	DEV_ASC_LENGTH			0xc0000
int dev_asc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_asc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_bt459.c:  */
#define	DEV_BT459_LENGTH		0x20
#define	DEV_BT459_NREGS			0x800
int dev_bt459_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_bt459_init(struct memory *mem, uint64_t baseaddr, unsigned char *rgb_palette, int color_fb_flag);

/*  dev_cons.c:  */
#define	DEV_CONS_ADDRESS		0x0000000010000000
#define	DEV_CONS_LENGTH			0x0000000000000020
#define	    DEV_CONS_PUTCHAR		    0x0000
#define	    DEV_CONS_GETCHAR		    0x0010
int dev_cons_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_cons_init(struct memory *mem);

/*  dev_colorplanemask.c:  */
#define	DEV_COLORPLANEMASK_LENGTH	0x0000000000000010
int dev_colorplanemask_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_colorplanemask_init(struct memory *mem, uint64_t baseaddr, unsigned char *color_plane_mask);

/*  dev_crime.c:  */
#define	DEV_CRIME_LENGTH		0x0000000000000100
int dev_crime_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_crime_init(struct memory *mem, uint64_t baseaddr);

/*  dev_dc7085.c:  */
#define	DEV_DC7085_LENGTH		0x0000000000000080
/*  see dc7085.h for more info  */
void dev_dc7085_tick(struct cpu *cpu, void *);
int dev_dc7085_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_dc7085_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb);

/*  dev_fb.c:  */
#define	DEV_FB_LENGTH			0x3c0000	/*  3c0000 to not colide with turbochannel rom, otherwise size = (4*1024*1024)  */
#define	VFB_GENERIC			0
#define	VFB_HPCMIPS			1
#define	VFB_DEC_VFB01			2
#define	VFB_DEC_VFB02			3
#define	VFB_DEC_MAXINE			4
#define	VFB_PLAYSTATION2		5
struct vfb_data {
	int		vfb_type;

	int		vfb_scaledown;

	int		xsize;
	int		ysize;
	int		bit_depth;

	unsigned char	color_plane_mask;

	int		bytes_per_line;		/*  cached  */

	int		visible_xsize;
	int		visible_ysize;

	size_t		framebuffer_size;
	int		x11_xsize, x11_ysize;

	int		update_x1, update_y1, update_x2, update_y2;

	/*  RGB palette for <= 8 bit modes:  (r,g,b bytes for each)  */
	unsigned char	rgb_palette[256 * 3];

	/*  These should always be in sync:  */
	unsigned char	*framebuffer;
	struct fb_window *fb_window;
};
#define	VFB_MFB_BT459			0x180000
#define	VFB_MFB_VRAM			0x200000
#define	VFB_CFB_BT459			0x200000
void dev_fb_tick(struct cpu *, void *);
int dev_fb_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data *dev_fb_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int vfb_type,
	int visible_xsize, int visible_ysize, int xsize, int ysize, int bit_depth, char *name);

/*  dev_gt.c:  */
#define	DEV_GT_LENGTH			0x0000000000001000
int dev_gt_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_gt_init(struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_gt.c:  */
#define	DEV_KN01_CSR_LENGTH		0x0000000000000004
int dev_kn01_csr_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_kn01_csr_init(struct memory *mem, uint64_t baseaddr, int color_fb);

/*  dev_pckbc.c:  */
#define	DEV_PCKBC_LENGTH		0x0000000000000010
int dev_pckbc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_pckbc_init(struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_kn02.c:  */
#define	DEV_KN02_LENGTH		0x10
struct kn02_csr {
	uint32_t	csr;
};
int dev_kn02_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct kn02_csr *dev_kn02_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_kn210.c:  */
#define	DEV_KN210_LENGTH		0x0000000000001000
int dev_kn210_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_kn210_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_kn230.c:  */
#define	DEV_KN230_LENGTH		0x1c00000
struct kn230_csr {
	uint32_t	csr;
};
int dev_kn230_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct kn230_csr *dev_kn230_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_le.c:  */
#define	DEV_LE_LENGTH			0x0000000000000100
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_le_init(struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr);

/*  dev_ns16550.c:  */
#define	DEV_NS16550_LENGTH		0x0000000000000008
/*  see comreg.h and ns16550reg.h for more info  */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ns16550_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult);

/*  dev_mace.c:  */
#define	DEV_MACE_LENGTH			0x100
int dev_mace_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mace_init(struct memory *mem, uint64_t baseaddr);

/*  dev_macepci.c:  */
#define	DEV_MACEPCI_LENGTH		0x1000
int dev_macepci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_macepci_init(struct memory *mem, uint64_t baseaddr);

/*  dev_mc146818.c:  */
#define	DEV_MC146818_LENGTH		0x0000000000000100
/*  see mc146818reg.h for more info  */
void dev_mc146818_tick(struct cpu *cpu, void *);
int dev_mc146818_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mc146818_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int pc_style_cmos, int addrdiv, int emulated_ips);

/*  dev_mp.c:  */
#define	DEV_MP_ADDRESS			0x0000000011000000
#define	DEV_MP_LENGTH			0x0000000000000100
#define	    DEV_MP_WHOAMI		    0x0000
#define	    DEV_MP_NCPUS		    0x0010
#define	    DEV_MP_STARTUPCPU		    0x0020
#define	    DEV_MP_STARTUPADDR		    0x0030
#define	    DEV_MP_PAUSE_ADDR		    0x0040
#define	    DEV_MP_PAUSE_CPU		    0x0050
#define	    DEV_MP_UNPAUSE_CPU		    0x0060
int dev_mp_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mp_init(struct memory *mem, struct cpu *cpus[]);

/*  dev_ps2_dmac.c:  */
#define	DEV_PS2_DMAC_LENGTH		0x0000000000010000
int dev_ps2_dmac_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_dmac_init(struct memory *mem, uint64_t baseaddr, struct memory *mem_gif);

/*  dev_ps2_gs.c:  */
#define	DEV_PS2_GIF_LENGTH		0x0000000000010000
int dev_ps2_gif_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_gif_init(struct memory *mem, uint64_t baseaddr);

/*  dev_ps2_gs.c:  */
#define	DEV_PS2_GS_LENGTH		0x0000000000002000
int dev_ps2_gs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_gs_init(struct memory *mem, uint64_t baseaddr);

/*  dev_rd94.c:  */
#define	DEV_RD94_LENGTH			0x1000
int dev_rd94_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_rd94_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sii.c:  */
#define	DEV_SII_LENGTH			0x0000000000000100
void dev_sii_tick(struct cpu *cpu, void *);
int dev_sii_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sii_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr);

/*  dev_scc.c:  */
#define	DEV_SCC_LENGTH			0x1000
int dev_scc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_scc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb);

/*  dev_ssc.c:  */
#define	DEV_SSC_LENGTH			0x1000
int dev_ssc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ssc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb);

/*  dev_turbochannel.c:  */
#define	DEV_TURBOCHANNEL_LEN		0x0470
int dev_turbochannel_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_turbochannel_init(struct cpu *cpu, struct memory *mem, int slot_nr, uint64_t baseaddr, uint64_t endaddr, char *device_name, int irq);

/*  dev_vdac.c:  */
#define	DEV_VDAC_LENGTH			0x20
#define	DEV_VDAC_MAPWA			    0x00
#define	DEV_VDAC_MAP			    0x04
#define	DEV_VDAC_MASK			    0x08
#define	DEV_VDAC_MAPRA			    0x0c
#define	DEV_VDAC_OVERWA			    0x10
#define	DEV_VDAC_OVER			    0x14
#define	DEV_VDAC_OVERRA			    0x1c
int dev_vdac_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_vdac_init(struct memory *mem, uint64_t baseaddr, unsigned char *rgb_palette, int color_fb_flag);


#endif	/*  DEVICES_H  */


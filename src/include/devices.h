#ifndef	DEVICES_H
#define	DEVICES_H

/*
 *  Copyright (C) 2003,2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: devices.h,v 1.49 2004-04-02 05:48:04 debug Exp $
 *
 *  Memory mapped devices:
 */

#include <inttypes.h>
#include <sys/types.h>

struct pci_data;

/* #ifdef WITH_X11
#include <X11/Xlib.h>
#endif */


/*  dev_dec5500_ioboard.c:  */
#define	DEV_DEC5500_IOBOARD_LENGTH		0x100000
int dev_dec5500_ioboard_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct dec5500_ioboard_data *dev_dec5500_ioboard_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_dec_ioasic.c:  */
#define	DEV_DEC_IOASIC_LENGTH		0xc0000
struct dec_ioasic_data {
	uint32_t	csr;
	uint32_t	intr;
	uint32_t	imsk;

	uint32_t	t1_dmaptr;
	uint32_t	t1_cur_ptr;
	uint32_t	t2_dmaptr;
	uint32_t	t2_cur_ptr;
};
int dev_dec_ioasic_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct dec_ioasic_data *dev_dec_ioasic_init(struct memory *mem, uint64_t baseaddr);

/*  dev_8250.c:  */
#define	DEV_8250_LENGTH		0x0000000000000008
int dev_8250_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_8250_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult);

/*  dev_asc.c:  */
#define	DEV_ASC_LENGTH			0xc0000
int dev_asc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_asc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_bt459.c:  */
#define	DEV_BT459_LENGTH		0x20
#define	DEV_BT459_NREGS			0x800
int dev_bt459_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data;
void dev_bt459_init(struct memory *mem, uint64_t baseaddr, struct vfb_data *vfb_data, int color_fb_flag);

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
#define	DEV_CRIME_LENGTH		0x0000000000001000
int dev_crime_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_crime_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_dc7085.c:  */
#define	DEV_DC7085_LENGTH		0x0000000000000080
/*  see dc7085.h for more info  */
void dev_dc7085_tick(struct cpu *cpu, void *);
int dev_dc7085_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_dc7085_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb);

/*  dev_dec5800.c:  */
#define	DEV_DEC5800_LENGTH			0x1000	/*  ?  */
struct dec5800_data {
        uint32_t        csr;
	uint32_t	vector_0x50;
};
int dev_dec5800_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct dec5800_data *dev_dec5800_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_deccca.c:  */
#define	DEV_DECCCA_LENGTH			0x10000	/*  ?  */
#define	DEC_DECCCA_BASEADDR			0x19000000	/*  ?  I just made this up  */
int dev_deccca_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_deccca_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_decxmi.c:  */
#define	DEV_DECXMI_LENGTH			0x800000
int dev_decxmi_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_decxmi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

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

	int		updated_last_tick;
	int		update_x1, update_y1, update_x2, update_y2;

	/*  RGB palette for <= 8 bit modes:  (r,g,b bytes for each)  */
	unsigned char	rgb_palette[256 * 3];

	int		cursor_x;
	int		cursor_y;
	int		cursor_xsize;
	int		cursor_ysize;
	int		cursor_on;
	int		OLD_cursor_x;
	int		OLD_cursor_y;
	int		OLD_cursor_xsize;
	int		OLD_cursor_ysize;
	int		OLD_cursor_on;

	/*  These should always be in sync:  */
	unsigned char	*framebuffer;
	struct fb_window *fb_window;
};
#define	VFB_MFB_BT459			0x180000
#define	VFB_MFB_VRAM			0x200000
#define	VFB_CFB_BT459			0x200000
void set_grayscale_palette(struct vfb_data *d, int ncolors);
void dev_fb_setcursor(struct vfb_data *d, int cursor_x, int cursor_y, int on, 
        int cursor_xsize, int cursor_ysize);
void framebuffer_blockcopyfill(struct vfb_data *d, int fillflag, int fill_r,
	int fill_g, int fill_b, int x1, int y1, int x2, int y2,
	int from_x, int from_y);
void dev_fb_tick(struct cpu *, void *);
int dev_fb_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data *dev_fb_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int vfb_type,
	int visible_xsize, int visible_ysize, int xsize, int ysize, int bit_depth, char *name);

/*  dev_fdc.c:  */
#define	DEV_FDC_LENGTH		0x0000000000000100
int dev_fdc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_fdc_init(struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_gt.c:  */
#define	DEV_GT_LENGTH			0x0000000000001000
int dev_gt_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct pci_data *dev_gt_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int pciirq);

/*  dev_gt.c:  */
#define	DEV_KN01_CSR_LENGTH		0x0000000000000004
int dev_kn01_csr_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_kn01_csr_init(struct memory *mem, uint64_t baseaddr, int color_fb);

/*  dev_pckbc.c:  */
#define	DEV_PCKBC_LENGTH		0x0000000000000100
#define	PCKBC_8042		0
#define	PCKBC_8242		1
int dev_pckbc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_pckbc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int type, int keyboard_irqnr, int mouse_irqnr);

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
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_le_init(struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr, int len);

/*  dev_n64_bios.c:  */
#define	DEV_N64_BIOS_LENGTH		(0x05000000 - 0x03f00000)
int dev_n64_bios_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_n64_bios_init(struct memory *mem, uint64_t baseaddr);

/*  dev_ns16550.c:  */
#define	DEV_NS16550_LENGTH		0x0000000000000008
/*  see comreg.h and ns16550reg.h for more info  */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ns16550_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult);

/*  dev_mace.c:  */
#define	DEV_MACE_LENGTH			0x100
int dev_mace_interrupt(int mace_irq);
int dev_mace_interrupt_ack(int mace_irq);
int dev_mace_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mace_init(struct memory *mem, uint64_t baseaddr, int irqnr);

/*  dev_macepci.c:  */
#define	DEV_MACEPCI_LENGTH		0x1000
int dev_macepci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct pci_data *dev_macepci_init(struct memory *mem, uint64_t baseaddr, int pciirq);

/*  dev_mc146818.c:  */
#define	DEV_MC146818_LENGTH		0x0000000000000100
#define	MC146818_DEC		0
#define	MC146818_PC_CMOS	1
#define	MC146818_ARC_NEC	2
#define	MC146818_SGI		3
/*  see mc146818reg.h for more info  */
void dev_mc146818_tick(struct cpu *cpu, void *);
int dev_mc146818_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mc146818_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int access_style, int addrdiv, int emulated_ips);

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

/*  dev_ps2_gs.c:  */
#define	DEV_PS2_GIF_LENGTH		0x0000000000010000
int dev_ps2_gif_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_gif_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_ps2_gs.c:  */
#define	DEV_PS2_GS_LENGTH		0x0000000000002000
int dev_ps2_gs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_gs_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_ps2_ohci.c:  */
#define	DEV_PS2_OHCI_LENGTH		0x1000
int dev_ps2_ohci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_ohci_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_ps2_spd.c:  */
#define	DEV_PS2_SPD_LENGTH		0x800
int dev_ps2_spd_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ps2_spd_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_ps2_stuff.c:  */
#include "ps2_dmacreg.h"
#define N_PS2_DMA_CHANNELS              10
#define	N_PS2_TIMERS			4
struct ps2_data {
	uint32_t	timer_count[N_PS2_TIMERS];
	uint32_t	timer_comp[N_PS2_TIMERS];
	uint32_t	timer_mode[N_PS2_TIMERS];
	uint32_t	timer_hold[N_PS2_TIMERS];	/*  NOTE: only 0 and 1 are valid  */

        uint64_t	dmac_reg[DMAC_REGSIZE / 0x10];

	struct memory	*other_memory[N_PS2_DMA_CHANNELS];

	uint32_t	intr;
	uint32_t	imask;
};
#define	DEV_PS2_STUFF_LENGTH		0x10000
int dev_ps2_stuff_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct ps2_data *dev_ps2_stuff_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, struct memory *mem_gif);

/*  dev_px.c:  */
struct px_data {
	struct memory	*fb_mem;
	struct vfb_data	*vfb_data;
	int		type;
	char		*px_name;
	int		irq_nr;
	int		bitdepth;
	int		xconfig;
	int		yconfig;

	uint32_t	intr;
	unsigned char	sram[128 * 1024];
};
/*  TODO: perhaps these types are wrong?  */
#define	DEV_PX_TYPE_PX			0
#define	DEV_PX_TYPE_PXG			1
#define	DEV_PX_TYPE_PXGPLUS		2
#define	DEV_PX_TYPE_PXGPLUSTURBO	3
#define	DEV_PX_LENGTH			0x3c0000
int dev_px_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_px_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int px_type, int irq_nr);

/*  dev_ram.c:  */
#define	DEV_RAM_RAM		0
#define	DEV_RAM_MIRROR		1
int dev_ram_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ram_init(struct memory *mem, uint64_t baseaddr, uint64_t length, int mode, uint64_t otheraddr);

/*  dev_rd94.c:  */
#define	DEV_RD94_LENGTH			0x1000
int dev_rd94_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct pci_data *dev_rd94_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int pciirq);

/*  dev_scc.c:  */
#define	DEV_SCC_LENGTH			0x1000
int dev_scc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_scc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb, int scc_nr, int addrmul);

/*  dev_sgec.c:  */
#define	DEV_SGEC_LENGTH		0x1000
int dev_sgec_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgec_init(struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_sgi_cpuinfo.c:  */
#define	DEV_SGI_CPUINFO_BASE		0x9600000000000000
#define	DEV_SGI_CPUINFO_LENGTH		0x0001000000000000
int dev_sgi_cpuinfo_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_cpuinfo_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_gbe.c:  */
#define	DEV_SGI_GBE_LENGTH		0x1000000
int dev_sgi_gbe_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_gbe_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_ip19.c:  */
#define	DEV_SGI_IP19_LENGTH		0x100000
int dev_sgi_ip19_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_ip19_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_ip22.c:  */
#define	DEV_SGI_IP22_LENGTH		0x100
int dev_sgi_ip22_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_ip22_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_mte.c:  */
#define	DEV_SGI_MTE_LENGTH		0x10000
int dev_sgi_mte_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_nasid.c:  */
#define	DEV_SGI_NASID_BASE		0x9200000000000000
#define	DEV_SGI_NASID_LENGTH		0x0000000100000000
int dev_sgi_nasid_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_nasid_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_ust.c:  */
#define	DEV_SGI_UST_LENGTH		0x10000
int dev_sgi_ust_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_ust_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sii.c:  */
#define	DEV_SII_LENGTH			0x0000000000000100
void dev_sii_tick(struct cpu *cpu, void *);
int dev_sii_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sii_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr);

/*  dev_ssc.c:  */
#define	DEV_SSC_LENGTH			0x1000
int dev_ssc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ssc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb, uint32_t *);

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

/*  dev_wdc.c:  */
#define	DEV_WDC_LENGTH			0x8
int dev_wdc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_wdc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int base_drive);

/*  dev_wdsc.c:  */
#define	DEV_WDSC_LENGTH			0x1000
int dev_wdsc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_wdsc_init(struct memory *mem, uint64_t baseaddr);

/*  dev_zs.c:  */
#define	DEV_ZS_LENGTH			0x8
int dev_zs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_zs_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult);

/*  lk201.c:  */
struct lk201_data {
        int                     use_fb;

        void                    (*add_to_rx_queue)(void *,int,int);
	void			*add_data;
                
        unsigned char           keyb_buf[8];
        int                     keyb_buf_pos;
                        
        int                     mouse_mode;
        int                     mouse_revision;         /*  0..15  */  
        int                     mouse_x, mouse_y, mouse_buttons;
};
void lk201_tick(struct lk201_data *); 
void lk201_tx_data(struct lk201_data *, int port, int idata);
void lk201_init(struct lk201_data *d, int use_fb, void (*add_to_rx_queue)(void *,int,int), void *);


#endif	/*  DEVICES_H  */


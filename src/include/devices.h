#ifndef	DEVICES_H
#define	DEVICES_H

/*
 *  Copyright (C) 2003-2004  Anders Gavare.  All rights reserved.
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
 *  $Id: devices.h,v 1.111 2004-12-09 00:04:10 debug Exp $
 *
 *  Memory mapped devices:
 */

#include <sys/types.h>
#include <inttypes.h>

struct cpu;
struct memory;
struct pci_data;

/* #ifdef WITH_X11
#include <X11/Xlib.h>
#endif */


/*  dev_dec5500_ioboard.c:  */
#define	DEV_DEC5500_IOBOARD_LENGTH		0x100000
int dev_dec5500_ioboard_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct dec5500_ioboard_data *dev_dec5500_ioboard_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_dec_ioasic.c:  */
#define	DEV_DEC_IOASIC_LENGTH		0x80100
#define	N_DEC_IOASIC_REGS	(0x1f0 / 0x10)
#define	MAX_IOASIC_DMA_FUNCTIONS	8
struct dec_ioasic_data {
	uint32_t	reg[N_DEC_IOASIC_REGS];
	int		(*(dma_func[MAX_IOASIC_DMA_FUNCTIONS]))(struct cpu *, void *, uint64_t addr, size_t dma_len, int tx);
	void		*dma_func_extra[MAX_IOASIC_DMA_FUNCTIONS];
};
int dev_dec_ioasic_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct dec_ioasic_data *dev_dec_ioasic_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_8250.c:  */
#define	DEV_8250_LENGTH		8
int dev_8250_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_8250_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult);

/*  dev_asc.c:  */
#define	DEV_ASC_DEC_LENGTH		0x40000
#define	DEV_ASC_PICA_LENGTH		0x1000
#define	DEV_ASC_DEC		1
#define	DEV_ASC_PICA		2
int dev_asc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_asc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	int irq_nr, void *turbochannel, int mode,
	size_t (*dma_controller)(void *dma_controller_data,
		unsigned char *data, size_t len, int writeflag),
	void *dma_controller_data);

/*  dev_au1x00.c:  */
struct au1x00_ic_data {
	int		ic_nr;
	uint32_t	request0_int;
	uint32_t	request1_int;
	uint32_t	config0;
	uint32_t	config1;
	uint32_t	config2;
	uint32_t	source;
	uint32_t	assign_request;
	uint32_t	wakeup;
	uint32_t	mask;
};

int dev_au1x00_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct au1x00_ic_data *dev_au1x00_init(struct cpu *cpu, struct memory *mem);

/*  dev_bt431.c:  */
#define	DEV_BT431_LENGTH		0x20
#define	DEV_BT431_NREGS			0x800	/*  ?  */
int dev_bt431_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data;
void dev_bt431_init(struct memory *mem, uint64_t baseaddr, struct vfb_data *vfb_data, int color_fb_flag);

/*  dev_bt455.c:  */
#define	DEV_BT455_LENGTH		0x20
int dev_bt455_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data;
void dev_bt455_init(struct memory *mem, uint64_t baseaddr, struct vfb_data *vfb_data);

/*  dev_bt459.c:  */
#define	DEV_BT459_LENGTH		0x20
#define	DEV_BT459_NREGS			0x1000
#define	BT459_PX		1	/*  px[g]  */
#define	BT459_BA		2	/*  cfb  */
#define	BT459_BBA		3	/*  sfb  */
int dev_bt459_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct vfb_data;
void dev_bt459_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	uint64_t baseaddr_irq, struct vfb_data *vfb_data, int color_fb_flag, int irq_nr, int type);

/*  dev_cons.c:  */
#define	DEV_CONS_ADDRESS		0x0000000010000000
#define	DEV_CONS_LENGTH			0x0000000000000020
#define	    DEV_CONS_PUTGETCHAR		    0x0000
#define	    DEV_CONS_HALT		    0x0010
int dev_cons_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_cons_init(struct memory *mem);

/*  dev_colorplanemask.c:  */
#define	DEV_COLORPLANEMASK_LENGTH	0x0000000000000010
int dev_colorplanemask_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_colorplanemask_init(struct memory *mem, uint64_t baseaddr, unsigned char *color_plane_mask);

/*  dev_crime.c:  */
#define	DEV_CRIME_LENGTH		0x0000000000001000
struct crime_data {
	unsigned char	reg[DEV_CRIME_LENGTH];
	int		irq_nr;
	int		use_fb;
};
int dev_crime_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct crime_data *dev_crime_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb);

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

/*  dev_decbi.c:  */
/*  16 slots, 0x2000 bytes each  */
#define	DEV_DECBI_LENGTH			0x20000
int dev_decbi_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_decbi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

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

	int		update_x1, update_y1, update_x2, update_y2;

	/*  RGB palette for <= 8 bit modes:  (r,g,b bytes for each)  */
	unsigned char	rgb_palette[256 * 3];

	/*  These should always be in sync:  */
	unsigned char	*framebuffer;
	struct fb_window *fb_window;
};
#define	VFB_MFB_BT455			0x100000
#define	VFB_MFB_BT431			0x180000
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
	int visible_xsize, int visible_ysize, int xsize, int ysize, int bit_depth, char *name, int logo);

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
#define	DEV_LE_LENGTH			0x1c0200
int dev_le_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_le_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr, int len);

/*  dev_m700_fb.c:  */
#define	DEV_M700_FB_LENGTH		0x10000		/*  TODO?  */
int dev_m700_fb_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_m700_fb_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, uint64_t baseaddr2);

/*  dev_n64_bios.c:  */
#define	DEV_N64_BIOS_LENGTH		(0x05000000 - 0x03f00000)
int dev_n64_bios_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_n64_bios_init(struct memory *mem, uint64_t baseaddr);

/*  dev_ns16550.c:  */
#define	DEV_NS16550_LENGTH		0x0000000000000008
/*  see comreg.h and ns16550reg.h for more info  */
int dev_ns16550_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ns16550_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult, int in_use);

/*  dev_mace.c:  */
#define	DEV_MACE_LENGTH			0x100
struct mace_data {
	unsigned char	reg[DEV_MACE_LENGTH];
	int		irqnr;
};
int dev_mace_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct mace_data *dev_mace_init(struct memory *mem, uint64_t baseaddr, int irqnr);

/*  dev_macepci.c:  */
#define	DEV_MACEPCI_LENGTH		0x1000
int dev_macepci_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct pci_data *dev_macepci_init(struct memory *mem, uint64_t baseaddr, int pciirq);

/*  dev_mc146818.c:  */
#define	DEV_MC146818_LENGTH		0x0000000000000100
#define	MC146818_DEC		0
#define	MC146818_PC_CMOS	1
#define	MC146818_ARC_NEC	2
#define	MC146818_ARC_PICA	3
#define	MC146818_SGI		4
/*  see mc146818reg.h for more info  */
void dev_mc146818_tick(struct cpu *cpu, void *);
int dev_mc146818_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mc146818_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int access_style, int addrdiv);

/*  dev_mp.c:  */
int dev_mp_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_mp_init(struct memory *mem, struct cpu *cpus[]);

/*  dev_pckbc.c:  */
#define	DEV_PCKBC_LENGTH		0x10
#define	PCKBC_8042		0
#define	PCKBC_8242		1
#define	PCKBC_PICA		3
int dev_pckbc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_pckbc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int type, int keyboard_irqnr, int mouse_irqnr, int in_use);

/*  dev_pica.c:  */
#define	DEV_PICA_LENGTH			0x280
struct pica_data {
	struct cpu	*cpu;

	/*  Jazz stuff:  */
	uint32_t	int_enable_mask;
	uint32_t	int_asserted;

	/*  ISA stuff:  */
	uint32_t	isa_int_enable_mask;
	uint32_t	isa_int_asserted;

	int		interval;
	int		interval_start;

	int		pica_timer_value;
	int		pica_timer_current;

	uint64_t	dma_translation_table_base;
	uint64_t	dma_translation_table_limit;
	uint32_t	dma0_mode;
	uint32_t	dma0_enable;
	uint32_t	dma0_count;
	uint32_t	dma0_addr;
	/*  same for dma1,2,3 actually (TODO)  */
};
size_t dev_pica_dma_controller(void *dma_controller_data,
	unsigned char *data, size_t len, int writeflag);
int dev_pica_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct pica_data *dev_pica_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

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

/*  dev_pmagja.c:  */
#define	DEV_PMAGJA_LENGTH		0x3c0000
int dev_pmagja_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_pmagja_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr);

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
int dev_scc_dma_func(struct cpu *cpu, void *extra, uint64_t addr, size_t dma_len, int tx);
void *dev_scc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb, int scc_nr, int addrmul);

/*  dev_sfb.c:  */
#define	DEV_SFB_LENGTH		0x400000
int dev_sfb_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sfb_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr,
	struct vfb_data *vfb_data);

/*  dev_sgec.c:  */
#define	DEV_SGEC_LENGTH		0x1000
int dev_sgec_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgec_init(struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_sgi_cpuinfo.c:  */
#define	DEV_SGI_CPUINFO_BASE		0x9600000000000000ULL
#define	DEV_SGI_CPUINFO_LENGTH		0x0001000000000000ULL
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

/*  dev_sgi_ip20.c:  */
#define	DEV_SGI_IP20_LENGTH		0x40
#define	DEV_SGI_IP20_BASE		0x1fb801c0
struct sgi_ip20_data {
	int		dummy;
};
int dev_sgi_ip20_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct sgi_ip20_data *dev_sgi_ip20_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_ip22.c:  */
#define	DEV_SGI_IP22_LENGTH		0x100
#define	DEV_SGI_IP22_IMC_LENGTH		0x100
#define	DEV_SGI_IP22_UNKNOWN2_LENGTH	0x100
#define	IP22_IMC_BASE			0x1fa00000
#define	IP22_UNKNOWN2_BASE		0x1fb94000
struct sgi_ip22_data {
	int		guiness_flag;
	uint32_t	reg[DEV_SGI_IP22_LENGTH / 4];
	uint32_t	imc_reg[DEV_SGI_IP22_IMC_LENGTH / 4];
	uint32_t	unknown2_reg[DEV_SGI_IP22_UNKNOWN2_LENGTH / 4];
	uint32_t	unknown_timer;
};
int dev_sgi_ip22_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
struct sgi_ip22_data *dev_sgi_ip22_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int guiness_flag);

/*  dev_sgi_ip30.c:  */
#define	DEV_SGI_IP30_LENGTH		0x80000
int dev_sgi_ip30_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_ip30_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_mardigras.c:  */
#define	DEV_SGI_MARDIGRAS_LENGTH	0x800000
int dev_sgi_mardigras_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_mardigras_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_mte.c:  */
#define	DEV_SGI_MTE_LENGTH		0x10000
int dev_sgi_mte_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_nasid.c:  */
#define	DEV_SGI_NASID_BASE		0x9200000000000000ULL
#define	DEV_SGI_NASID_LENGTH		0x0000000100000000ULL
int dev_sgi_nasid_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_nasid_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sgi_ust.c:  */
#define	DEV_SGI_UST_LENGTH		0x10000
int dev_sgi_ust_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sgi_ust_init(struct memory *mem, uint64_t baseaddr);

/*  dev_sii.c:  */
#define	DEV_SII_LENGTH			0x100
void dev_sii_tick(struct cpu *cpu, void *);
int dev_sii_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sii_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, uint64_t buf_start, uint64_t buf_end, int irq_nr);

/*  dev_sn_fb.c:  */
#define	DEV_SN_LENGTH		0x1000
int dev_sn_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_sn_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr);

/*  dev_ssc.c:  */
#define	DEV_SSC_LENGTH			0x1000
int dev_ssc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_ssc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int use_fb, uint32_t *);

/*  dev_turbochannel.c:  */
#define	DEV_TURBOCHANNEL_LEN		0x0470
int dev_turbochannel_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_turbochannel_init(struct cpu *cpu, struct memory *mem, int slot_nr, uint64_t baseaddr, uint64_t endaddr, char *device_name, int irq);

/*  dev_unreadable.c:  */
int dev_unreadable_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
int dev_random_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra);
int dev_zero_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra);
void dev_unreadable_init(struct memory *mem, uint64_t baseaddr, uint64_t len);
void dev_random_init(struct memory *mem, uint64_t baseaddr, uint64_t len);
void dev_zero_init(struct memory *mem, uint64_t baseaddr, uint64_t len);

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

/*  dev_vga.c:  */
int dev_vga_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_vga_init(struct cpu *cpu, struct memory *mem, uint64_t videomem_base, uint64_t control_base, int max_x, int max_y, char *name);

/*  dev_wdc.c:  */
#define	DEV_WDC_LENGTH			0x8
int dev_wdc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_wdc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int base_drive);

/*  dev_wdsc.c:  */
#define	DEV_WDSC_NREGS			0x100		/*  8-bit register select  */
#define	DEV_WDSC_LENGTH			0x10
int dev_wdsc_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *);
void dev_wdsc_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int controller_nr, int irq_nr);

/*  dev_zs.c:  */
#define	DEV_ZS_LENGTH			0x10
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

	int			old_host_mouse_x;
	int			old_host_mouse_y;
	int			old_host_mouse_stays_put;
	int			mouse_check_interval;
	int			mouse_check_interval_reset;
};
void lk201_tick(struct lk201_data *); 
void lk201_tx_data(struct lk201_data *, int port, int idata);
void lk201_init(struct lk201_data *d, int use_fb, void (*add_to_rx_queue)(void *,int,int), void *);


#endif	/*  DEVICES_H  */


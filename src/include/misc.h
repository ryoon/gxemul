#ifndef	MISC_H
#define	MISC_H

/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: misc.h,v 1.208 2005-01-23 11:19:37 debug Exp $
 *
 *  Misc. definitions for mips64emul.
 */


#include <sys/types.h>
#include <inttypes.h>

/*
 *  ../config.h contains #defines set by the configure script. Some of these
 *  might reduce speed of the emulator, so don't enable them unless you
 *  need them.
 */

#include "../config.h"

/*  
 *  ENABLE_INSTRUCTION_DELAYS should be defined on the cc commandline using
 *  -D if you want it. (This is done by ./configure --delays)
 */
#define USE_TINY_CACHE
/*  #define ALWAYS_SIGNEXTEND_32  */
/*  #define HALT_IF_PC_ZERO  */

#ifdef WITH_X11
#include <X11/Xlib.h>
#endif

#ifdef NO_MAP_ANON
#ifdef mmap
#undef mmap
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
static void *no_map_anon_mmap(void *addr, size_t len, int prot, int flags,
	int nonsense_fd, off_t offset)
{
	void *p;
	int fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open /dev/zero\n");
		exit(1);
	}

	printf("addr=%p len=%lli prot=0x%x flags=0x%x nonsense_fd=%i "
	    "offset=%16lli\n", addr, (long long) len, prot, flags,
	    nonsense_fd, (long long) offset);

	p = mmap(addr, len, prot, flags, fd, offset);

	printf("p = %p\n", p);

	/*  TODO: Close the descriptor?  */
	return p;
}
#define mmap no_map_anon_mmap
#endif


struct emul;
struct machine;


/*  Debug stuff:  */
#define	DEBUG_BUFSIZE		1024

#define	DEFAULT_RAM_IN_MB		32
#define	MAX_DEVICES			24

#define	DEVICE_STATE_TYPE_INT		1
#define	DEVICE_STATE_TYPE_UINT64_T	2

struct cpu;

struct memory {
	uint64_t	physical_max;
	void		*pagetable;

	int		n_mmapped_devices;
	int		last_accessed_device;
	/*  The following two might speed up things a little bit.  */
	/*  (actually maxaddr is the addr after the last address)  */
	uint64_t	mmap_dev_minaddr;
	uint64_t	mmap_dev_maxaddr;

	const char	*dev_name[MAX_DEVICES];
	uint64_t	dev_baseaddr[MAX_DEVICES];
	uint64_t	dev_length[MAX_DEVICES];
	int		dev_flags[MAX_DEVICES];
	void		*dev_extra[MAX_DEVICES];
	int		(*dev_f[MAX_DEVICES])(struct cpu *,struct memory *,
			    uint64_t,unsigned char *,size_t,int,void *);
	int		(*dev_f_state[MAX_DEVICES])(struct cpu *,
			    struct memory *, void *extra, int wf, int nr,
			    int *type, char **namep, void **data, size_t *len);
	unsigned char	*dev_bintrans_data[MAX_DEVICES];
#ifdef BINTRANS
	uint64_t	dev_bintrans_write_low[MAX_DEVICES];
	uint64_t	dev_bintrans_write_high[MAX_DEVICES];
#endif
};

#define	BITS_PER_PAGETABLE	20
#define	BITS_PER_MEMBLOCK	20
#define	MAX_BITS		40

#define	MEM_READ			0
#define	MEM_WRITE			1


#define	MAX_TICK_FUNCTIONS	12

#define	CACHE_DATA			0
#define	CACHE_INSTRUCTION		1
#define	CACHE_NONE			2

#define	CACHE_FLAGS_MASK		0x3

#define	NO_EXCEPTIONS			8
#define	PHYSICAL			16

#define	EMUL_LITTLE_ENDIAN		0
#define	EMUL_BIG_ENDIAN			1

#define	DEFAULT_NCPUS			1


/*  main.c:  */
void debug_indentation(int diff);
void debug(char *fmt, ...);
void fatal(char *fmt, ...);


/*  arcbios.c:  */
void arcbios_add_string_to_component(char *string, uint64_t component);
void arcbios_console_init(struct cpu *cpu,
	uint64_t vram, uint64_t ctrlregs, int maxx, int maxy);
void arcbios_register_scsicontroller(uint64_t scsicontroller_component);
uint64_t arcbios_get_scsicontroller(void);
void arcbios_add_memory_descriptor(struct cpu *cpu,
	uint64_t base, uint64_t len, int arctype);
uint64_t arcbios_addchild_manual(struct cpu *cpu,
	uint64_t class, uint64_t type, uint64_t flags, uint64_t version,
	uint64_t revision, uint64_t key, uint64_t affinitymask,
	char *identifier, uint64_t parent, void *config_data,
	size_t config_len);
void arcbios_emul(struct cpu *cpu);
void arcbios_set_64bit_mode(int enable);
void arcbios_set_default_exception_handler(struct cpu *cpu);
void arcbios_init(void);


/*  debugger:  */
void debugger_activate(int x);
void debugger(void);
void debugger_init(struct emul **emuls, int n_emuls);


/*  dec_prom.c:  */
void decstation_prom_emul(struct cpu *cpu);


/*  file.c:  */
int file_n_executables_loaded(void);
void file_load(struct memory *mem, char *filename, struct cpu *cpu);


/*  mips16.c:  */
int mips16_to_32(struct cpu *cpu, unsigned char *instr16, unsigned char *instr);


/*  ps2_bios.c:  */
void playstation2_sifbios_emul(struct cpu *cpu);


/*  useremul.c:  */
#define	USERLAND_NONE		0
#define	USERLAND_NETBSD_PMAX	1
#define	USERLAND_ULTRIX_PMAX	2
void useremul_init(struct cpu *, int, char **);
void useremul_syscall(struct cpu *cpu, uint32_t code);


/*  x11.c:  */
#define N_GRAYCOLORS            16
#define	CURSOR_COLOR_TRANSPARENT	-1
#define	CURSOR_COLOR_INVERT		-2
#define	CURSOR_MAXY		64
#define	CURSOR_MAXX		64
/*  Framebuffer windows:  */
struct fb_window {
	int		fb_number;

#ifdef WITH_X11
	/*  x11_fb_winxsize > 0 for a valid fb_window  */
	int		x11_fb_winxsize, x11_fb_winysize;
	int		scaledown;
	Display		*x11_display;

	int		x11_screen;
	int		x11_screen_depth;
	unsigned long	fg_color;
	unsigned long	bg_color;
	XColor		x11_graycolor[N_GRAYCOLORS];
	Window		x11_fb_window;
	GC		x11_fb_gc;

	XImage		*fb_ximage;
	unsigned char	*ximage_data;

	/*  -1 means transparent, 0 and up are grayscales  */
	int		cursor_pixels[CURSOR_MAXY][CURSOR_MAXX];
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

	/*  Host's X11 cursor:  */
	Cursor		host_cursor;
	Pixmap		host_cursor_pixmap;
#endif
};
void x11_redraw_cursor(int);
void x11_redraw(int);
void x11_putpixel_fb(int, int x, int y, int color);
#ifdef WITH_X11
void x11_putimage_fb(int);
#endif
void x11_init(struct machine *);
struct fb_window *x11_fb_init(int xsize, int ysize, char *name,
	int scaledown, struct machine *);
void x11_check_event(void);


#endif	/*  MISC_H  */

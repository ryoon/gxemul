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
 *  $Id: cpu_rca180x_instr.c,v 1.2 2006-12-28 12:09:33 debug Exp $
 *
 *  RCA180X instructions.
 *
 *  See http://www.elf-emulation.com/1802.html for a good list of 1802/1805
 *  opcodes.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (n_translated_instrs is automatically increased by 1 for each function
 *  call. If no instruction was executed, then it should be decreased. If, say,
 *  4 instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 *
 *  NOTE/TODO: This file still contains CHIP8 instructions only...
 */


/*****************************************************************************/


static void rca180x_putpixel(struct cpu *cpu, int x, int y, int color)
{
	/*  TODO: Optimize.  */
	int sx, sy;
	uint8_t pixel = color? 255 : 0;
	int linelen = cpu->cd.rca180x.xres;
	uint64_t addr = (linelen * y * cpu->machine->x11_scaleup + x)
	    * cpu->machine->x11_scaleup + CHIP8_FB_ADDR;

	cpu->cd.rca180x.framebuffer_cache[y * cpu->cd.rca180x.xres + x] = pixel;

	linelen = (linelen - 1) * cpu->machine->x11_scaleup;

	for (sy=0; sy<cpu->machine->x11_scaleup; sy++) {
		for (sx=0; sx<cpu->machine->x11_scaleup; sx++) {
			cpu->memory_rw(cpu, cpu->mem, addr, &pixel,
			    sizeof(pixel), MEM_WRITE, PHYSICAL);
			addr ++;
		}
		addr += linelen;
	}
}


/*
 *  cls: Clear screen.
 */
X(cls)
{
	/*  TODO: Optimize.  */
	int x, y;
	for (y=0; y<cpu->cd.rca180x.yres; y++)
		for (x=0; x<cpu->cd.rca180x.xres; x++)
			rca180x_putpixel(cpu, x, y, 0);
}


/*
 *  sprite:  Draw a 8 pixel wide sprite.
 *
 *  arg[0] = ptr to register containing x coordinate
 *  arg[1] = ptr to register containing y coordinate
 *  arg[2] = height
 */
X(sprite)
{
	int xb = *((uint8_t *)ic->arg[0]), yb = *((uint8_t *)ic->arg[1]);
	int x, y, height = ic->arg[2];
	int index = cpu->cd.rca180x.index;

	/*  Synchronize the PC first:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.rca180x.cur_ic_page)
	    / sizeof(struct rca180x_instr_call);
	cpu->pc &= ~((RCA180X_IC_ENTRIES_PER_PAGE-1)
	    << RCA180X_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << RCA180X_INSTR_ALIGNMENT_SHIFT);

	/*  debug("[ rca180x sprite at x=%i y=%i, height=%i ]\n",
	    xb, yb, height);  */
	cpu->cd.rca180x.v[15] = 0;

	for (y=yb; y<yb+height; y++) {
		uint8_t color;
		cpu->memory_rw(cpu, cpu->mem, index++, &color,
		    sizeof(color), MEM_READ, PHYSICAL);
		for (x=xb; x<xb+8; x++) {
			int xc = x % cpu->cd.rca180x.xres;
			int yc = y % cpu->cd.rca180x.yres;
			if (cpu->cd.rca180x.framebuffer_cache[yc *
			    cpu->cd.rca180x.xres + xc]) {
				color ^= 0x80;
				if ((color & 0x80) == 0)
					cpu->cd.rca180x.v[15] = 1;
			}
			rca180x_putpixel(cpu, xc, yc, color & 0x80);
			color <<= 1;
		}
	}

	cpu->n_translated_instrs += 200000;
	cpu->pc += 2;
	cpu->cd.rca180x.next_ic = &nothing_call;
}


/*
 *  mov:  rx = ry
 *  or:   rx = rx | ry
 *  and:  rx = rx & ry
 *  xor:  rx = rx ^ ry
 *  add:  rx = rx + ry, vf set to 1 on carry overflow
 *  sub:  rx = rx - ry, vf set to 1 on borrow
 *
 *  arg[0] = ptr to register x
 *  arg[1] = ptr to register y
 */
X(mov) { (*((uint8_t *)ic->arg[0])) = *((uint8_t *)ic->arg[1]); }
X(or)  { (*((uint8_t *)ic->arg[0])) |= *((uint8_t *)ic->arg[1]); }
X(and) { (*((uint8_t *)ic->arg[0])) &= *((uint8_t *)ic->arg[1]); }
X(xor) { (*((uint8_t *)ic->arg[0])) ^= *((uint8_t *)ic->arg[1]); }
X(add)
{
	int x = *((uint8_t *)ic->arg[0]);
	int y = *((uint8_t *)ic->arg[1]);
	x += y;
	*((uint8_t *)ic->arg[0]) = x;
	cpu->cd.rca180x.v[15] = (x > 255);
}
X(sub)
{
	int x = *((uint8_t *)ic->arg[0]);
	int y = *((uint8_t *)ic->arg[1]);
	/*  VF bit = negated borrow  */
	cpu->cd.rca180x.v[15] = (x >= y);
	*((uint8_t *)ic->arg[0]) = x - y;
}


/*
 *  skeq_imm:  Skip next instruction if a register is equal to a constant.
 *
 *  arg[0] = ptr to register
 *  arg[1] = 8-bit constant
 */
X(skeq_imm)
{
	if (*((uint8_t *)ic->arg[0]) == ic->arg[1])
		cpu->cd.rca180x.next_ic ++;
}


/*
 *  skne_imm:  Skip next instruction if a register is not equal to a constant.
 *
 *  arg[0] = ptr to register
 *  arg[1] = 8-bit constant
 */
X(skne_imm)
{
	if (*((uint8_t *)ic->arg[0]) != ic->arg[1])
		cpu->cd.rca180x.next_ic ++;
}


/*
 *  skeq:  Skip next instruction if a register is equal to another.
 *  skne:  Skip next instruction if a register is not equal to another.
 *
 *  arg[0] = ptr to register x
 *  arg[1] = ptr to register y
 */
X(skeq)
{
	if (*((uint8_t *)ic->arg[0]) == *((uint8_t *)ic->arg[1]))
		cpu->cd.rca180x.next_ic ++;
}
X(skne)
{
	if (*((uint8_t *)ic->arg[0]) != *((uint8_t *)ic->arg[1]))
		cpu->cd.rca180x.next_ic ++;
}


/*
 *  mov_imm:  Move constant to register.
 *
 *  arg[0] = ptr to register
 *  arg[1] = 8-bit constant
 */
X(mov_imm)
{
	(*((uint8_t *)ic->arg[0])) = ic->arg[1];
}


/*
 *  jmp:  Jump to a fixed addres (always on the same page).
 *
 *  arg[0] = ptr to new instruction
 */
X(jmp)
{
	cpu->cd.rca180x.next_ic = (struct rca180x_instr_call *) ic->arg[0];
}


/*
 *  jsr:  Jump to a subroutine at a fixed addres (always on the same page).
 *
 *  arg[0] = ptr to new instruction
 */
X(jsr)
{
	uint16_t pc12;

	/*  Synchronize the PC first:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.rca180x.cur_ic_page)
	    / sizeof(struct rca180x_instr_call);
	cpu->pc &= ~((RCA180X_IC_ENTRIES_PER_PAGE-1)
	    << RCA180X_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << RCA180X_INSTR_ALIGNMENT_SHIFT);
	pc12 = (cpu->pc & 0xfff) + sizeof(uint16_t);

	/*  Push return address to the stack:  */
	cpu->cd.rca180x.sp -= sizeof(uint16_t);
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.rca180x.sp,
	    (unsigned char *)&pc12, sizeof(pc12), MEM_WRITE, PHYSICAL);

	cpu->cd.rca180x.next_ic = (struct rca180x_instr_call *) ic->arg[0];
}


/*
 *  rts:  Return from a subroutine.
 */
X(rts)
{
	uint16_t pc12;

	/*  Pop return address to the stack:  */
	cpu->memory_rw(cpu, cpu->mem, cpu->cd.rca180x.sp,
	    (unsigned char *)&pc12, sizeof(pc12), MEM_READ, PHYSICAL);
	cpu->cd.rca180x.sp += sizeof(uint16_t);

	cpu->pc = pc12 & 0xfff;
	quick_pc_to_pointers(cpu);
}


/*
 *  add_imm:  Add constant to register, without updating the carry bit.
 *
 *  arg[0] = ptr to register
 *  arg[1] = 8-bit constant
 */
X(add_imm)
{
	(*((uint8_t *)ic->arg[0])) += ic->arg[1];
}


/*
 *  rand:  Set a register to a random value.
 *
 *  arg[0] = ptr to register
 *  arg[1] = 8-bit constant
 */
X(rand)
{
	/*  http://www.pdc.kth.se/~lfo/rca180x/RCA180X.htm says AND,
	    http://members.aol.com/autismuk/rca180x/rca180xdef.htm says %.  */
	(*((uint8_t *)ic->arg[0])) = random() & ic->arg[1];
}


/*
 *  skpr:  Skip next instruction if key is pressed.
 *  skup:  Skip next instruction if key is up.
 *
 *  arg[0] = key number
 */
X(skpr)
{
	/*  TODO  */
}
X(skup)
{
	/*  TODO  */
	cpu->cd.rca180x.next_ic ++;
}


/*
 *  gdelay:  Get the timer delay value.
 *  sdelay:  Set the timer delay value.
 *  ssound:  Set the sound delay value.
 *
 *  arg[0] = ptr to register
 */
X(gdelay) { *((uint8_t *)ic->arg[0]) = cpu->cd.rca180x.delay_timer_value; }
X(sdelay) { cpu->cd.rca180x.delay_timer_value = *((uint8_t *)ic->arg[0]); }
X(ssound) { cpu->cd.rca180x.sound_timer_value = *((uint8_t *)ic->arg[0]); }


/*
 *  adi:  Add a register's value to the Index register.
 *
 *  arg[0] = ptr to register
 */
X(adi)
{
	cpu->cd.rca180x.index += *((uint8_t *)ic->arg[0]);
}


/*
 *  font:  Set the Index register to point to a font sprite.
 *
 *  arg[0] = ptr to register containing the hex char
 */
X(font)
{
	int c = *((uint8_t *)ic->arg[0]);
	if (c > 0xf)
		fatal("[ rca180x font: WARNING: c = 0x%02x ]\n", c);
	cpu->cd.rca180x.index = CHIP8_FONT_ADDR + 5 * (c & 0xf);
}


/*
 *  bcd:  Store BCD representation of a register in memory.
 *
 *  arg[0] = ptr to register
 */
X(bcd)
{
	int r = *((uint8_t *)ic->arg[0]);
	uint8_t a[3];
	a[0] = r / 100, a[1] = (r / 10) % 10, a[2] = r % 10;

	cpu->memory_rw(cpu, cpu->mem, cpu->cd.rca180x.index,
	    (unsigned char *) &a, sizeof(a), MEM_WRITE, PHYSICAL);
}


/*
 *  str:  Store multiple registers to memory.
 *
 *  arg[0] = last register number (note: not pointer)
 */
X(str)
{
	int r;
	for (r=0; r<=ic->arg[0]; r++) {
		cpu->memory_rw(cpu, cpu->mem, cpu->cd.rca180x.index++,
		    &cpu->cd.rca180x.v[r], sizeof(uint8_t), MEM_WRITE,
		    PHYSICAL);
	}
}


/*
 *  ldr:  Load multiple registers from memory.
 *
 *  arg[0] = last register number (note: not pointer)
 */
X(ldr)
{
	int r;
	for (r=0; r<=ic->arg[0]; r++) {
		cpu->memory_rw(cpu, cpu->mem, cpu->cd.rca180x.index++,
		    &cpu->cd.rca180x.v[r], sizeof(uint8_t), MEM_READ, PHYSICAL);
	}
}


/*
 *  mvi:  Move constant to Index register.
 *
 *  arg[0] = 12-bit constant
 */
X(mvi)
{
	cpu->cd.rca180x.index = ic->arg[0];
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Should never happen on RCA180X, because that would mean that we
	    are running outside of available memory.  */

	fatal("[ rca180x end of page reached, halting ]\n");

	cpu->running = 0;
	debugger_n_steps_left_before_interaction = 0;
	cpu->cd.rca180x.next_ic = &nothing_call;

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;
}


/*****************************************************************************/


/*
 *  rca180x_instr_to_be_translated():
 *
 *  Translate an instruction word into an rca180x_instr_call. ic is filled in
 *  with valid data for the translated instruction, or a "nothing" instruction
 *  if there was a translation failure. The newly translated instruction is
 *  then executed.
 */
X(to_be_translated)
{
	int addr, low_pc, dst_addr;
	unsigned char ib[2];
	unsigned char *page;

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.rca180x.cur_ic_page)
	    / sizeof(struct rca180x_instr_call);
	addr = cpu->pc & ~((RCA180X_IC_ENTRIES_PER_PAGE-1) <<
	    RCA180X_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << RCA180X_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = addr;
	addr &= ~((1 << RCA180X_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	page = cpu->cd.avr.host_load[addr >> 12];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), sizeof(uint16_t));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(uint16_t), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): "
			    "read failed: TODO\n");
			exit(1);
		}
	}


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	switch (ib[0] >> 4) {

	case 0x0:
		if (ib[0] == 0x00 && ib[1] == 0xe0) {
			ic->f = instr(cls);
		} else if (ib[0] == 0x00 && ib[1] == 0xee) {
			ic->f = instr(rts);
		} else {
			goto bad;
		}
		break;

	case 0x1:
		ic->f = instr(jmp);
		dst_addr = ((ib[0] & 0xf) << 8) + ib[1];
		ic->arg[0] = (size_t) (cpu->cd.rca180x.cur_ic_page +
		    (dst_addr >> RCA180X_INSTR_ALIGNMENT_SHIFT));
		break;

	case 0x2:
		ic->f = instr(jsr);
		dst_addr = ((ib[0] & 0xf) << 8) + ib[1];
		ic->arg[0] = (size_t) (cpu->cd.rca180x.cur_ic_page +
		    (dst_addr >> RCA180X_INSTR_ALIGNMENT_SHIFT));
		break;

	case 0x3:
		ic->f = instr(skeq_imm);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = ib[1];
		break;

	case 0x4:
		ic->f = instr(skne_imm);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = ib[1];
		break;

	case 0x5:
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = (size_t) &cpu->cd.rca180x.v[ib[1] >> 4];
		switch (ib[1] & 0xf) {
		case 0x0: ic->f = instr(skeq); break;
		default:goto bad;
		}
		break;

	case 0x6:
		ic->f = instr(mov_imm);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = ib[1];
		break;

	case 0x7:
		ic->f = instr(add_imm);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = ib[1];
		break;

	case 0x8:
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = (size_t) &cpu->cd.rca180x.v[ib[1] >> 4];
		switch (ib[1] & 0xf) {
		case 0x0: ic->f = instr(mov); break;
		case 0x1: ic->f = instr(or); break;
		case 0x2: ic->f = instr(and); break;
		case 0x3: ic->f = instr(xor); break;
		case 0x4: ic->f = instr(add); break;
		case 0x5: ic->f = instr(sub); break;
		default:goto bad;
		}
		break;

	case 0x9:
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = (size_t) &cpu->cd.rca180x.v[ib[1] >> 4];
		switch (ib[1] & 0xf) {
		case 0x0: ic->f = instr(skne); break;
		default:goto bad;
		}
		break;

	case 0xa:
		ic->f = instr(mvi);
		ic->arg[0] = ((ib[0] & 0xf) << 8) + ib[1];
		break;

	case 0xc:
		ic->f = instr(rand);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = ib[1];
		break;

	case 0xd:
		ic->f = instr(sprite);
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		ic->arg[1] = (size_t) &cpu->cd.rca180x.v[ib[1] >> 4];
		ic->arg[2] = ib[1] & 0xf;

		if (ic->arg[2] == 0) {
			fatal("xsprite: TODO\n");
			goto bad;
		}
		break;

	case 0xe:
		/*  Default arg 0:  */
		ic->arg[0] = ib[0] & 0xf;
		switch (ib[1]) {
		case 0x9e: ic->f = instr(skpr); break;
		case 0xa1: ic->f = instr(skup); break;
		default:goto bad;
		}
		break;

	case 0xf:
		/*  Default arg 0:  */
		ic->arg[0] = (size_t) &cpu->cd.rca180x.v[ib[0] & 0xf];
		switch (ib[1]) {
		case 0x07:
			ic->f = instr(gdelay);
			break;
		case 0x15:
			ic->f = instr(sdelay);
			break;
		case 0x18:
			ic->f = instr(ssound);
			break;
		case 0x1e:
			ic->f = instr(adi);
			break;
		case 0x29:
			ic->f = instr(font);
			break;
		case 0x33:
			ic->f = instr(bcd);
			break;
		case 0x55:
			ic->f = instr(str);
			ic->arg[0] = ib[0] & 0xf;
			break;
		case 0x65:
			ic->f = instr(ldr);
			ic->arg[0] = ib[0] & 0xf;
			break;
		default:goto bad;
		}
		break;

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}


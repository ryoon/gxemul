/*
 *  Copyright (C) 2005-2006  Anders Gavare.  All rights reserved.
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
 *  $Id: cpu_sh_instr.c,v 1.12 2006-09-16 01:33:27 debug Exp $
 *
 *  SH instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


/*
 *  nop: Nothing
 */
X(nop)
{
}


/*
 *  mov_rm_rn:     rn = rm
 *  neg_rm_rn:     rn = -rm
 *  not_rm_rn:     rn = ~rm
 *  swap_b_rm_rn:  rn = rm with lowest 2 bytes swapped
 *  swap_w_rm_rn:  rn = rm with high and low 16-bit words swapped
 *  exts_b_rm_rn:  rn = (int8_t) rm
 *  extu_b_rm_rn:  rn = (uint8_t) rm
 *  exts_w_rm_rn:  rn = (int16_t) rm
 *  extu_w_rm_rn:  rn = (uint16_t) rm
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(mov_rm_rn)    { reg(ic->arg[1]) = reg(ic->arg[0]); }
X(not_rm_rn)    { reg(ic->arg[1]) = ~reg(ic->arg[0]); }
X(neg_rm_rn)    { reg(ic->arg[1]) = -reg(ic->arg[0]); }
X(swap_b_rm_rn)
{
	uint32_t r = reg(ic->arg[0]);
	reg(ic->arg[1]) = (r & 0xffff0000) | ((r >> 8)&0xff) | ((r&0xff) << 8);
}
X(swap_w_rm_rn)
{
	uint32_t r = reg(ic->arg[0]);
	reg(ic->arg[1]) = (r >> 16) | (r << 16);
}
X(exts_b_rm_rn) { reg(ic->arg[1]) = (int8_t)reg(ic->arg[0]); }
X(extu_b_rm_rn) { reg(ic->arg[1]) = (uint8_t)reg(ic->arg[0]); }
X(exts_w_rm_rn) { reg(ic->arg[1]) = (int16_t)reg(ic->arg[0]); }
X(extu_w_rm_rn) { reg(ic->arg[1]) = (uint16_t)reg(ic->arg[0]); }


/*
 *  and_imm_r0:  r0 &= imm
 *  xor_imm_r0:  r0 ^= imm
 *  or_imm_r0:   r0 |= imm
 *
 *  arg[0] = imm
 */
X(and_imm_r0) { cpu->cd.sh.r[0] &= ic->arg[0]; }
X(xor_imm_r0) { cpu->cd.sh.r[0] ^= ic->arg[0]; }
X(or_imm_r0)  { cpu->cd.sh.r[0] |= ic->arg[0]; }


/*
 *  mov_imm_rn:  Set rn to a signed 8-bit value
 *  add_imm_rn:  Add a signed 8-bit value to Rn
 *
 *  arg[0] = int8_t imm, extended to at least int32_t
 *  arg[1] = ptr to rn
 */
X(mov_imm_rn) { reg(ic->arg[1]) = (int32_t)ic->arg[0]; }
X(add_imm_rn) { reg(ic->arg[1]) += (int32_t)ic->arg[0]; }


/*
 *  mov_l_rm_predec_rn:  mov.l Rm,@-Rn
 *             and also  sts.l PR,@-Rn
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(mov_l_rm_predec_rn)
{
	uint32_t addr = reg(ic->arg[1]) - sizeof(uint32_t);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_store[addr >> 12];
	uint32_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 2] = data;
		reg(ic->arg[1]) = addr;
		return;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		   sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_l_rm_predec_rn: write failed: TODO\n");
			exit(1);
		}
		/*  The store was ok:  */
		reg(ic->arg[1]) = addr;
	}
}


/*
 *  mov_l_disp_pc_rn:  Load a 32-bit value into a register,
 *                     from an immediate address relative to the pc.
 *
 *  arg[0] = offset from beginning of the current pc's page
 *  arg[1] = ptr to rn
 */
X(mov_l_disp_pc_rn)
{
	uint32_t addr = ic->arg[0] + (cpu->pc &
	    ~((SH_IC_ENTRIES_PER_PAGE-1) << SH_INSTR_ALIGNMENT_SHIFT));
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_load[addr >> 12];

	if (p != NULL) {
		uint32_t data = p[(addr & 0xfff) >> 2];
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			data = LE32_TO_HOST(data);
		else
			data = BE32_TO_HOST(data);
		reg(ic->arg[1]) = data;
		return;
	}

	/*  Slow, using memory_rw():  */
	fatal("slow mov_l using memory_rw: TODO\n");
	exit(1);
}


/*
 *  mova_r0:  Set r0 to an address close to the program counter.
 *
 *  arg[0] = relative offset from beginning of the current pc's page
 */
X(mova_r0)
{
	cpu->cd.sh.r[0] = ic->arg[0] + (cpu->pc &
	    ~((SH_IC_ENTRIES_PER_PAGE-1) << SH_INSTR_ALIGNMENT_SHIFT));
}


/*
 *  mov_w_disp_pc_rn:  Load a 16-bit value into a register,
 *                     from an immediate address relative to the pc.
 *
 *  arg[0] = offset from beginning of the current pc's page
 *  arg[1] = ptr to rn
 */
X(mov_w_disp_pc_rn)
{
	uint32_t addr = ic->arg[0] + (cpu->pc &
	    ~((SH_IC_ENTRIES_PER_PAGE-1) << SH_INSTR_ALIGNMENT_SHIFT));
	uint16_t *p = (uint16_t *) cpu->cd.sh.host_load[addr >> 12];

	if (p != NULL) {
		uint16_t data = p[(addr & 0xfff) >> 1];
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			data = LE16_TO_HOST(data);
		else
			data = BE16_TO_HOST(data);
		reg(ic->arg[1]) = (int16_t)data;
		return;
	}

	/*  Slow, using memory_rw():  */
	fatal("slow mov_w using memory_rw: TODO\n");
	exit(1);
}


/*
 *  load_b_rm_rn:      Load an 8-bit value into Rn from address Rm.
 *  load_l_rm_rn:      Load a 32-bit value into Rn from address Rm.
 *  mov_b_r0_rm_rn:    Load an int8_t into Rn from address Rm + R0.
 *  mov_l_disp_rm_rn:  Load a 32-bit value into Rn from address Rm + disp.
 *  load_arg1_postinc_to_arg0:
 *
 *  arg[0] = ptr to rm   (or rm + (lo4 << 4) for disp)
 *  arg[1] = ptr to rn
 */
X(load_b_rm_rn)
{
	uint32_t addr = reg(ic->arg[0]);
	uint8_t *p = (uint8_t *) cpu->cd.sh.host_load[addr >> 12];
	uint8_t data;

	if (p != NULL) {
		data = p[addr & 0xfff];
	} else {
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_READ, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_load_rm_rn: read failed: TODO\n");
			exit(1);
		}
	}
	reg(ic->arg[1]) = data;
}
X(load_l_rm_rn)
{
	uint32_t addr = reg(ic->arg[0]);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_load[addr >> 12];
	uint32_t data;

	if (p != NULL) {
		data = p[(addr & 0xfff) >> 2];
	} else {
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_READ, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_load_rm_rn: read failed: TODO\n");
			exit(1);
		}
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);
	reg(ic->arg[1]) = data;
}
X(load_arg1_postinc_to_arg0)
{
	uint32_t addr = reg(ic->arg[1]);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_load[addr >> 12];
	uint32_t data;

	if (p != NULL) {
		data = p[(addr & 0xfff) >> 2];
	} else {
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_READ, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_load_rm_rn: read failed: TODO\n");
			exit(1);
		}
	}
	/*  The load was ok:  */
	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);
	reg(ic->arg[1]) = addr + sizeof(uint32_t);
	reg(ic->arg[0]) = data;
}
X(mov_b_r0_rm_rn)
{
	uint32_t addr = reg(ic->arg[0]) + cpu->cd.sh.r[0];
	int8_t *p = (int8_t *) cpu->cd.sh.host_load[addr >> 12];
	int8_t data;

	if (p != NULL) {
		data = p[addr & 0xfff];
	} else {
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_READ, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_load_rm_rn: read failed: TODO\n");
			exit(1);
		}
	}

	reg(ic->arg[1]) = data;
}
X(mov_l_disp_rm_rn)
{
	uint32_t addr = cpu->cd.sh.r[ic->arg[0] & 0xf] +
	    ((ic->arg[0] >> 4) << 2);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_load[addr >> 12];
	uint32_t data;

	if (p != NULL) {
		data = p[(addr & 0xfff) >> 2];
	} else {
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_READ, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_load_rm_rn: read failed: TODO\n");
			exit(1);
		}
	}

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);
	reg(ic->arg[1]) = data;
}


/*
 *  mov_b_store_rm_rn:  Store Rm to address Rn (8-bit).
 *  mov_w_store_rm_rn:  Store Rm to address Rn (16-bit).
 *  mov_l_store_rm_rn:  Store Rm to address Rn (32-bit).
 *  mov_l_rm_r0_rn:     Store Rm to address Rn + R0.
 *  mov_l_rm_disp_rn:   Store Rm to address disp + Rn.
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn    (or  Rn+(disp<<4)  for mov_l_rm_disp_rn)
 */
X(mov_b_store_rm_rn)
{
	uint32_t addr = reg(ic->arg[1]);
	uint8_t *p = (uint8_t *) cpu->cd.sh.host_store[addr >> 12];
	uint8_t data = reg(ic->arg[0]);

	if (p != NULL) {
		p[addr & 0xfff] = data;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, &data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_store_rm_rn: write failed: TODO\n");
			exit(1);
		}
	}
}
X(mov_w_store_rm_rn)
{
	uint32_t addr = reg(ic->arg[1]);
	uint16_t *p = (uint16_t *) cpu->cd.sh.host_store[addr >> 12];
	uint16_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE16_TO_HOST(data);
	else
		data = BE16_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 1] = data;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_store_rm_rn: write failed: TODO\n");
			exit(1);
		}
	}
}
X(mov_l_store_rm_rn)
{
	uint32_t addr = reg(ic->arg[1]);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_store[addr >> 12];
	uint32_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 2] = data;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("mov_store_rm_rn: write failed: TODO\n");
			exit(1);
		}
	}
}
X(mov_l_rm_r0_rn)
{
	uint32_t addr = reg(ic->arg[1]) + cpu->cd.sh.r[0];
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_store[addr >> 12];
	uint32_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 2] = data;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("write failed: TODO\n");
			exit(1);
		}
	}
}
X(mov_l_rm_disp_rn)
{
	uint32_t addr = cpu->cd.sh.r[ic->arg[1] & 0xf] +
	    ((ic->arg[1] >> 4) << 2);
	uint32_t *p = (uint32_t *) cpu->cd.sh.host_store[addr >> 12];
	uint32_t data = reg(ic->arg[0]);

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		data = LE32_TO_HOST(data);
	else
		data = BE32_TO_HOST(data);

	if (p != NULL) {
		p[(addr & 0xfff) >> 2] = data;
	} else {
		/*  Slow, using memory_rw():  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, (unsigned char *)&data,
		    sizeof(data), MEM_WRITE, CACHE_DATA)) {
			/*  This should probably be ok, but I just want
				to catch it when it happens...  */
			fatal("write failed: TODO\n");
			exit(1);
		}
	}
}


/*
 *  add_rm_rn:  rn = rn + rm
 *  and_rm_rn:  rn = rn & rm
 *  xor_rm_rn:  rn = rn ^ rm
 *  or_rm_rn:   rn = rn | rm
 *  sub_rm_rn:  rn = rn - rm
 *  tst_rm_rn:  t = ((rm & rn) == 0)
 *  xtrct_rm_rn:  rn = (rn >> 16) | (rm << 16)
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(add_rm_rn) { reg(ic->arg[1]) += reg(ic->arg[0]); }
X(and_rm_rn) { reg(ic->arg[1]) &= reg(ic->arg[0]); }
X(xor_rm_rn) { reg(ic->arg[1]) ^= reg(ic->arg[0]); }
X(or_rm_rn)  { reg(ic->arg[1]) |= reg(ic->arg[0]); }
X(sub_rm_rn) { reg(ic->arg[1]) -= reg(ic->arg[0]); }
X(tst_rm_rn)
{
	if (reg(ic->arg[1]) & reg(ic->arg[0]))
		cpu->cd.sh.sr &= ~SH_SR_T;
	else
		cpu->cd.sh.sr |= SH_SR_T;
}
X(xtrct_rm_rn)
{
	uint32_t rn = reg(ic->arg[1]), rm = reg(ic->arg[0]);
	reg(ic->arg[1]) = (rn >> 16) | (rm << 16);
}


/*
 *  mul_l_rm_rn:   MACL = Rm * Rn       (32-bit)
 *  dmulu_l_rm_rn: MACH:MACL = Rm * Rn  (64-bit)
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(mul_l_rm_rn)
{
	cpu->cd.sh.macl = reg(ic->arg[0]) * reg(ic->arg[1]);
}
X(dmulu_l_rm_rn)
{
	uint64_t rm = reg(ic->arg[0]), rn = reg(ic->arg[1]), res;
	res = rm * rn;
	cpu->cd.sh.mach = (uint32_t) (res >> 32);
	cpu->cd.sh.macl = (uint32_t) res;
}


/*
 *  cmpeq_imm_r0: rn == int8_t immediate
 *  cmpeq_rm_rn:  rn == rm
 *  cmphs_rm_rn:  rn >= rm, unsigned
 *  cmpge_rm_rn:  rn >= rm, signed
 *  cmphi_rm_rn:  rn > rm, unsigned
 *  cmpgt_rm_rn:  rn > rm, signed
 *  cmppz_rn:     rn >= 0, signed
 *  cmppl_rn:     rn > 0, signed
 *
 *  arg[0] = ptr to rm   (or imm, for cmpeq_imm_r0)
 *  arg[1] = ptr to rn
 */
X(cmpeq_imm_r0)
{
	if (cpu->cd.sh.r[0] == (uint32_t)ic->arg[0])
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmpeq_rm_rn)
{
	if (reg(ic->arg[1]) == reg(ic->arg[0]))
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmphs_rm_rn)
{
	if (reg(ic->arg[1]) >= reg(ic->arg[0]))
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmpge_rm_rn)
{
	if ((int32_t)reg(ic->arg[1]) >= (int32_t)reg(ic->arg[0]))
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmphi_rm_rn)
{
	if (reg(ic->arg[1]) > reg(ic->arg[0]))
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmpgt_rm_rn)
{
	if ((int32_t)reg(ic->arg[1]) > (int32_t)reg(ic->arg[0]))
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmppz_rn)
{
	if ((int32_t)reg(ic->arg[1]) >= 0)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}
X(cmppl_rn)
{
	if ((int32_t)reg(ic->arg[1]) > 0)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
}


/*
 *  shll_rn:  Shift rn left by 1  (t = bit that was shifted out)
 *  shlr_rn:  Shift rn right by 1 (t = bit that was shifted out)
 *  shar_rn:  Shift rn right arithmetically by 1 (t = bit that was shifted out)
 *  shllX_rn: Shift rn left logically by X bits
 *  shlrX_rn: Shift rn right logically by X bits
 *
 *  arg[1] = ptr to rn
 */
X(shll_rn)
{
	uint32_t rn = reg(ic->arg[1]);
	if (rn >> 31)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
	reg(ic->arg[1]) = rn << 1;
}
X(shlr_rn)
{
	uint32_t rn = reg(ic->arg[1]);
	if (rn & 1)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
	reg(ic->arg[1]) = rn >> 1;
}
X(shar_rn)
{
	int32_t rn = reg(ic->arg[1]);
	if (rn & 1)
		cpu->cd.sh.sr |= SH_SR_T;
	else
		cpu->cd.sh.sr &= ~SH_SR_T;
	reg(ic->arg[1]) = rn >> 1;
}
X(shll2_rn) { reg(ic->arg[1]) <<= 2; }
X(shll8_rn) { reg(ic->arg[1]) <<= 8; }
X(shll16_rn) { reg(ic->arg[1]) <<= 16; }
X(shlr2_rn) { reg(ic->arg[1]) >>= 2; }
X(shlr8_rn) { reg(ic->arg[1]) >>= 8; }
X(shlr16_rn) { reg(ic->arg[1]) >>= 16; }


/*
 *  shld: Shift Rn left or right, as indicated by Rm. Place result in Rn.
 *
 *  arg[0] = ptr to rm
 *  arg[1] = ptr to rn
 */
X(shld)
{
	uint32_t rn = reg(ic->arg[1]);
	int32_t rm = reg(ic->arg[0]);
	int sa = rm & 0x1f;

	if (rm >= 0) {
		rn <<= sa;
	} else if (sa != 0) {
		rn >>= sa;
	} else
		rn = 0;

	reg(ic->arg[1]) = rn;
}


/*
 *  bra: Branch (with delay-slot)
 *
 *  arg[0] = immediate offset relative to start of page
 */
X(bra)
{
	MODE_int_t target = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	target += ic->arg[0];
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		/*  Note: Must be non-delayed when jumping to the new pc:  */
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  bt: Branch if true
 *  bf: Branch if false
 *  bt/s: Branch if true (with delay-slot)
 *  bf/s: Branch if false (with delay-slot)
 *
 *  arg[0] = immediate offset relative to start of page
 */
X(bt)
{
	if (cpu->cd.sh.sr & SH_SR_T) {
		cpu->pc &= ~((SH_IC_ENTRIES_PER_PAGE-1) <<
		    SH_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += ic->arg[0];
		quick_pc_to_pointers(cpu);
	}
}
X(bf)
{
	if (!(cpu->cd.sh.sr & SH_SR_T)) {
		cpu->pc &= ~((SH_IC_ENTRIES_PER_PAGE-1) <<
		    SH_INSTR_ALIGNMENT_SHIFT);
		cpu->pc += ic->arg[0];
		quick_pc_to_pointers(cpu);
	}
}
X(bt_s)
{
	MODE_int_t target = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	int cond = cpu->cd.sh.sr & SH_SR_T;
	target += ic->arg[0];
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->delay_slot = NOT_DELAYED;
		if (cond) {
			cpu->pc = target;
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.sh.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(bf_s)
{
	MODE_int_t target = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	int cond = !(cpu->cd.sh.sr & SH_SR_T);
	target += ic->arg[0];
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->delay_slot = NOT_DELAYED;
		if (cond) {
			cpu->pc = target;
			quick_pc_to_pointers(cpu);
		} else
			cpu->cd.sh.next_ic ++;
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  jmp_rn: Jump to Rn
 *  jsr_rn: Jump to Rn, store return address in PR.
 *
 *  arg[0] = ptr to rn
 */
X(jmp_rn)
{
	MODE_int_t target = reg(ic->arg[0]);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jmp_rn_trace)
{
	MODE_int_t target = reg(ic->arg[0]);
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
#if 0
		/*  NOTE: Jmp works like both a return, and a subroutine
		    call.  */
		cpu_functioncall_trace_return(cpu);
		cpu_functioncall_trace(cpu, cpu->pc);
#endif
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jsr_rn)
{
	MODE_int_t target = reg(ic->arg[0]), retaddr;
	cpu->delay_slot = TO_BE_DELAYED;
	retaddr = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.sh.pr = retaddr + (int32_t)ic->arg[1];
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(jsr_rn_trace)
{
	MODE_int_t target = reg(ic->arg[0]), retaddr;
	cpu->delay_slot = TO_BE_DELAYED;
	retaddr = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	cpu->cd.sh.pr = retaddr + (int32_t)ic->arg[1];
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		cpu_functioncall_trace(cpu, cpu->pc);
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  rts: Jump to PR.
 */
X(rts)
{
	MODE_int_t target = cpu->cd.sh.pr;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}
X(rts_trace)
{
	MODE_int_t target = cpu->cd.sh.pr;
	cpu->delay_slot = TO_BE_DELAYED;
	ic[1].f(cpu, ic+1);
	cpu->n_translated_instrs ++;
	if (!(cpu->delay_slot & EXCEPTION_IN_DELAY_SLOT)) {
		cpu->pc = target;
		cpu_functioncall_trace_return(cpu);
		cpu->delay_slot = NOT_DELAYED;
		quick_pc_to_pointers(cpu);
	} else
		cpu->delay_slot = NOT_DELAYED;
}


/*
 *  sts_mach_rn: Store MACH into Rn
 *  sts_macl_rn: Store MACL into Rn
 *
 *  arg[1] = ptr to rn
 */
X(sts_mach_rn) { reg(ic->arg[1]) = cpu->cd.sh.mach; }
X(sts_macl_rn) { reg(ic->arg[1]) = cpu->cd.sh.macl; }


/*
 *  stc_sr_rn: Store SR into Rn
 *
 *  arg[1] = ptr to rn
 */
X(stc_sr_rn)
{
	if (!(cpu->cd.sh.sr & SH_SR_MD)) {
		fatal("TODO: Throw RESINST exception, if MD = 0.\n");
		exit(1);
	}

	reg(ic->arg[1]) = cpu->cd.sh.sr;
}


/*
 *  ldc_rm_sr: Store Rm into SR
 *
 *  arg[0] = ptr to rm
 */
X(ldc_rm_sr)
{
	if (!(cpu->cd.sh.sr & SH_SR_MD)) {
		fatal("TODO: Throw RESINST exception, if MD = 0.\n");
		exit(1);
	}

	sh_update_sr(cpu, reg(ic->arg[0]));
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((SH_IC_ENTRIES_PER_PAGE-1) <<
	    SH_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (SH_IC_ENTRIES_PER_PAGE << SH_INSTR_ALIGNMENT_SHIFT);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;

	/*
	 *  Find the new physpage and update translation pointers.
	 *
	 *  Note: This may cause an exception, if e.g. the new page is
	 *  not accessible.
	 */
	quick_pc_to_pointers(cpu);

	/*  Simple jump to the next page (if we are lucky):  */
	if (cpu->delay_slot == NOT_DELAYED)
		return;

	/*
	 *  If we were in a delay slot, and we got an exception while doing
	 *  quick_pc_to_pointers, then return. The function which called
	 *  end_of_page should handle this case.
	 */
	if (cpu->delay_slot == EXCEPTION_IN_DELAY_SLOT)
		return;

	/*
	 *  Tricky situation; the delay slot is on the next virtual page.
	 *  Calling to_be_translated will translate one instruction manually,
	 *  execute it, and then discard it.
	 */
	/*  fatal("[ end_of_page: delay slot across page boundary! ]\n");  */

	instr(to_be_translated)(cpu, cpu->cd.sh.next_ic);

	/*  The instruction in the delay slot has now executed.  */
	/*  fatal("[ end_of_page: back from executing the delay slot, %i ]\n",
	    cpu->delay_slot);  */

	/*  Find the physpage etc of the instruction in the delay slot
	    (or, if there was an exception, the exception handler):  */
	quick_pc_to_pointers(cpu);
}


X(end_of_page2)
{
	/*  Synchronize PC on the _second_ instruction on the next page:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.sh.cur_ic_page)
	    / sizeof(struct sh_instr_call);
	cpu->pc &= ~((SH_IC_ENTRIES_PER_PAGE-1)
	    << SH_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << SH_INSTR_ALIGNMENT_SHIFT);

	/*  This doesn't count as an executed instruction.  */
	cpu->n_translated_instrs --;

	quick_pc_to_pointers(cpu);

	if (cpu->delay_slot == NOT_DELAYED)
		return;

	fatal("end_of_page2: fatal error, we're in a delay slot\n");
	exit(1);
}


/*****************************************************************************/


/*
 *  sh_instr_to_be_translated():
 *
 *  Translate an instruction word into an sh_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint64_t addr, low_pc;
	uint32_t iword;
	unsigned char *page;
	unsigned char ib[4];
	int main_opcode, isize = cpu->cd.sh.compact? 2 : sizeof(ib);
	int in_crosspage_delayslot = 0, r8, r4, lo4, lo8;
	/*  void (*samepage_function)(struct cpu *, struct sh_instr_call *);  */

	/*  Figure out the (virtual) address of the instruction:  */
	low_pc = ((size_t)ic - (size_t)cpu->cd.sh.cur_ic_page)
	    / sizeof(struct sh_instr_call);

	/*  Special case for branch with delayslot on the next page:  */
	if (cpu->delay_slot == TO_BE_DELAYED && low_pc == 0) {
		/*  fatal("[ delay-slot translation across page "
		    "boundary ]\n");  */
		in_crosspage_delayslot = 1;
	}

	addr = cpu->pc & ~((SH_IC_ENTRIES_PER_PAGE-1)
	    << SH_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << SH_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (MODE_int_t)addr;
	addr &= ~((1 << SH_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
#ifdef MODE32
	page = cpu->cd.sh.host_load[(uint32_t)addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.sh.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xfff), isize);
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    isize, MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	if (cpu->cd.sh.compact) {
		iword = *((uint16_t *)&ib[0]);
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iword = LE16_TO_HOST(iword);
		else
			iword = BE16_TO_HOST(iword);
		main_opcode = iword >> 12;
		r8 = (iword >> 8) & 0xf;
		r4 = (iword >> 4) & 0xf;
		lo8 = iword & 0xff;
		lo4 = iword & 0xf;
	} else {
		iword = *((uint32_t *)&ib[0]);
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iword = LE32_TO_HOST(iword);
		else
			iword = BE32_TO_HOST(iword);
		main_opcode = -1;	/*  TODO  */
		fatal("SH5/SH64 isn't implemented yet. Sorry.\n");
		goto bad;
	}


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	/*  Default args. for many instructions:  */
	ic->arg[0] = (size_t)&cpu->cd.sh.r[r4];	/* m */
	ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */

	switch (main_opcode) {

	case 0x0:
		if (lo4 == 0x6) {
			/*  MOV.L Rm,@(R0,Rn)  */
			ic->f = instr(mov_l_rm_r0_rn);
		} else if (lo4 == 0x7) {
			/*  MUL.L Rm,Rn  */
			ic->f = instr(mul_l_rm_rn);
		} else if (iword == 0x000b) {
			if (cpu->machine->show_trace_tree)
				ic->f = instr(rts_trace);
			else
				ic->f = instr(rts);
		} else if (lo4 == 0xc) {
			/*  MOV.B @(R0,Rm),Rn  */
			ic->f = instr(mov_b_r0_rm_rn);
		} else {
			switch (lo8) {
			case 0x02:	/*  STC SR,Rn  */
				ic->f = instr(stc_sr_rn);
				break;
			case 0x09:	/*  NOP  */
				ic->f = instr(nop);
				if (iword & 0x0f00) {
					fatal("Unimplemented NOP variant?\n");
					goto bad;
				}
				break;
			case 0x0a:	/*  STS MACH,Rn  */
				ic->f = instr(sts_mach_rn);
				break;
			case 0x1a:	/*  STS MACL,Rn  */
				ic->f = instr(sts_macl_rn);
				break;
			default:fatal("Unimplemented opcode 0x%x,0x%03x\n",
				    main_opcode, iword & 0xfff);
				goto bad;
			}
		}
		break;

	case 0x1:
		ic->f = instr(mov_l_rm_disp_rn);
		ic->arg[1] = r8 + (lo4 << 4);
		break;

	case 0x2:
		switch (lo4) {
		case 0x0:	/*  MOV.B Rm,@Rn  */
			ic->f = instr(mov_b_store_rm_rn);
			break;
		case 0x1:	/*  MOV.W Rm,@Rn  */
			ic->f = instr(mov_w_store_rm_rn);
			break;
		case 0x2:	/*  MOV.L Rm,@Rn  */
			ic->f = instr(mov_l_store_rm_rn);
			break;
		case 0x6:	/*  MOV.L Rm,@-Rn  */
			ic->f = instr(mov_l_rm_predec_rn);
			break;
		case 0x8:	/*  TST Rm,Rn  */
			ic->f = instr(tst_rm_rn);
			break;
		case 0x9:	/*  AND Rm,Rn  */
			ic->f = instr(and_rm_rn);
			break;
		case 0xa:	/*  XOR Rm,Rn  */
			ic->f = instr(xor_rm_rn);
			break;
		case 0xb:	/*  OR Rm,Rn  */
			ic->f = instr(or_rm_rn);
			break;
		case 0xd:	/*  XTRCT Rm,Rn  */
			ic->f = instr(xtrct_rm_rn);
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, lo4);
			goto bad;
		}
		break;

	case 0x3:
		switch (lo4) {
		case 0x0:	/*  CMP/EQ Rm,Rn  */
			ic->f = instr(cmpeq_rm_rn);
			break;
		case 0x2:	/*  CMP/HS Rm,Rn  */
			ic->f = instr(cmphs_rm_rn);
			break;
		case 0x3:	/*  CMP/GE Rm,Rn  */
			ic->f = instr(cmpge_rm_rn);
			break;
		case 0x5:	/*  DMULU.L Rm,Rn  */
			ic->f = instr(dmulu_l_rm_rn);
			break;
		case 0x6:	/*  CMP/HI Rm,Rn  */
			ic->f = instr(cmphi_rm_rn);
			break;
		case 0x7:	/*  CMP/GT Rm,Rn  */
			ic->f = instr(cmpgt_rm_rn);
			break;
		case 0x8:	/*  SUB Rm,Rn  */
			ic->f = instr(sub_rm_rn);
			break;
		case 0xc:	/*  ADD Rm,Rn  */
			ic->f = instr(add_rm_rn);
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, lo4);
			goto bad;
		}
		break;

	case 0x4:
		if (lo4 == 0xd) {
			ic->f = instr(shld);
		} else {
			switch (lo8) {
			case 0x00:	/*  SHLL Rn  */
				ic->f = instr(shll_rn);
				break;
			case 0x01:	/*  SHLR Rn  */
				ic->f = instr(shlr_rn);
				break;
			case 0x08:	/*  SHLL2 Rn  */
				ic->f = instr(shll2_rn);
				break;
			case 0x09:	/*  SHLR2 Rn  */
				ic->f = instr(shlr2_rn);
				break;
			case 0x0b:	/*  JSR @Rn  */
				if (cpu->machine->show_trace_tree)
					ic->f = instr(jsr_rn_trace);
				else
					ic->f = instr(jsr_rn);
				ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* n */
				ic->arg[1] = (addr & 0xffe) + 4;
				break;
			case 0x0e:	/*  LDC Rm,SR  */
				ic->f = instr(ldc_rm_sr);
				ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* m */
				break;
			case 0x11:	/*  CMP/PZ Rn  */
				ic->f = instr(cmppz_rn);
				break;
			case 0x15:	/*  CMP/PL Rn  */
				ic->f = instr(cmppl_rn);
				break;
			case 0x18:	/*  SHLL8 Rn  */
				ic->f = instr(shll8_rn);
				break;
			case 0x19:	/*  SHLR8 Rn  */
				ic->f = instr(shlr8_rn);
				break;
			case 0x20:	/*  SHAL Rn  */
				ic->f = instr(shll_rn);  /*  NOTE: shll  */
				break;
			case 0x21:	/*  SHAR Rn  */
				ic->f = instr(shar_rn);
				break;
			case 0x22:	/*  STS.L PR,@-Rn  */
				ic->f = instr(mov_l_rm_predec_rn);
				ic->arg[0] = (size_t)&cpu->cd.sh.pr;	/* m */
				ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
				break;
			case 0x26:	/*  LDS.L @Rm+,PR  */
				ic->f = instr(load_arg1_postinc_to_arg0);
				ic->arg[0] = (size_t)&cpu->cd.sh.pr;
				break;
			case 0x28:	/*  SHLL16 Rn  */
				ic->f = instr(shll16_rn);
				break;
			case 0x29:	/*  SHLR16 Rn  */
				ic->f = instr(shlr16_rn);
				break;
			case 0x2b:	/*  JMP @Rn  */
				if (cpu->machine->show_trace_tree)
					ic->f = instr(jmp_rn_trace);
				else
					ic->f = instr(jmp_rn);
				ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* n */
				ic->arg[1] = (addr & 0xffe) + 4;
				break;
			default:fatal("Unimplemented opcode 0x%x,0x%02x\n",
				    main_opcode, lo8);
				goto bad;
			}
		}
		break;

	case 0x5:
		ic->f = instr(mov_l_disp_rm_rn);
		ic->arg[0] = r4 + (lo4 << 4);
		break;

	case 0x6:
		switch (lo4) {
		case 0x0:	/*  MOV.B @Rm,Rn  */
			ic->f = instr(load_b_rm_rn);
			break;
		case 0x2:	/*  MOV.L @Rm,Rn  */
			ic->f = instr(load_l_rm_rn);
			break;
		case 0x3:	/*  MOV Rm,Rn  */
			ic->f = instr(mov_rm_rn);
			break;
		case 0x6:	/*  MOV.L @Rm+,Rn  */
			ic->f = instr(load_arg1_postinc_to_arg0);
			/*  Note: Order  */
			ic->arg[1] = (size_t)&cpu->cd.sh.r[r4];	/* m */
			ic->arg[0] = (size_t)&cpu->cd.sh.r[r8];	/* n */
			break;
		case 0x7:	/*  NOT Rm,Rn  */
			ic->f = instr(not_rm_rn);
			break;
		case 0x8:	/*  SWAP.B Rm,Rn  */
			ic->f = instr(swap_b_rm_rn);
			break;
		case 0x9:	/*  SWAP.W Rm,Rn  */
			ic->f = instr(swap_w_rm_rn);
			break;
		case 0xb:	/*  NEG Rm,Rn  */
			ic->f = instr(neg_rm_rn);
			break;
		case 0xc:	/*  EXTU.B Rm,Rn  */
			ic->f = instr(extu_b_rm_rn);
			break;
		case 0xd:	/*  EXTU.W Rm,Rn  */
			ic->f = instr(extu_w_rm_rn);
			break;
		case 0xe:	/*  EXTS.B Rm,Rn  */
			ic->f = instr(exts_b_rm_rn);
			break;
		case 0xf:	/*  EXTS.W Rm,Rn  */
			ic->f = instr(exts_w_rm_rn);
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, lo4);
			goto bad;
		}
		break;

	case 0x7:	/*  ADD #imm,Rn  */
		ic->f = instr(add_imm_rn);
		ic->arg[0] = (int8_t)lo8;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	case 0x8:
		/*  Displacement from beginning of page = default arg 0.  */
		ic->arg[0] = (int8_t)lo8 * 2 +
		    (addr & ((SH_IC_ENTRIES_PER_PAGE-1)
		    << SH_INSTR_ALIGNMENT_SHIFT) & ~1) + 4;
		switch (r8) {
		case 0x8:	/*  CMP/EQ #imm,R0  */
			ic->f = instr(cmpeq_imm_r0);
			ic->arg[0] = (int8_t)lo8;
			break;
		case 0x9:	/*  BT (disp,PC)  */
			ic->f = instr(bt);
			break;
		case 0xb:	/*  BF (disp,PC)  */
			ic->f = instr(bf);
			break;
		case 0xd:	/*  BT/S (disp,PC)  */
			ic->f = instr(bt_s);
			break;
		case 0xf:	/*  BF/S (disp,PC)  */
			ic->f = instr(bf_s);
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, r8);
			goto bad;
		}
		break;

	case 0x9:	/*  MOV.L @(disp,PC),Rn  */
		ic->f = instr(mov_w_disp_pc_rn);
		ic->arg[0] = lo8 * 2 + (addr & ((SH_IC_ENTRIES_PER_PAGE-1)
		    << SH_INSTR_ALIGNMENT_SHIFT) & ~1) + 4;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	case 0xa:	/*  BRA disp  */
		ic->f = instr(bra);
		ic->arg[0] = (int32_t) ( (addr & ((SH_IC_ENTRIES_PER_PAGE-1)
		    << SH_INSTR_ALIGNMENT_SHIFT) & ~1) + 4 +
		    (((int32_t)(int16_t)((iword & 0xfff) << 4)) >> 3) );
		break;

	case 0xc:
		switch (r8) {
		case 0x7:	/*  MOVA @(disp,pc),R0  */
			ic->f = instr(mova_r0);
			ic->arg[0] = lo8 * 4 + (addr &
			    ((SH_IC_ENTRIES_PER_PAGE-1)
			    << SH_INSTR_ALIGNMENT_SHIFT) & ~3) + 4;
			break;
		case 0x9:	/*  AND #imm,R0  */
			ic->f = instr(and_imm_r0);
			ic->arg[0] = lo8;
			break;
		case 0xa:	/*  XOR #imm,R0  */
			ic->f = instr(xor_imm_r0);
			ic->arg[0] = lo8;
			break;
		case 0xb:	/*  OR #imm,R0  */
			ic->f = instr(or_imm_r0);
			ic->arg[0] = lo8;
			break;
		default:fatal("Unimplemented opcode 0x%x,0x%x\n",
			    main_opcode, r8);
			goto bad;
		}
		break;

	case 0xd:	/*  MOV.L @(disp,PC),Rn  */
		ic->f = instr(mov_l_disp_pc_rn);
		ic->arg[0] = lo8 * 4 + (addr & ((SH_IC_ENTRIES_PER_PAGE-1)
		    << SH_INSTR_ALIGNMENT_SHIFT) & ~3) + 4;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	case 0xe:	/*  MOV #imm,Rn  */
		ic->f = instr(mov_imm_rn);
		ic->arg[0] = (int8_t)lo8;
		ic->arg[1] = (size_t)&cpu->cd.sh.r[r8];	/* n */
		break;

	default:fatal("Unimplemented main opcode 0x%x\n", main_opcode);
		goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}



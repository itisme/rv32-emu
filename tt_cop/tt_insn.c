/*
 * tt_insn.c - Tensix instruction jump table and stub implementations
 *
 * This file contains the instruction dispatch table for all Tensix coprocessor
 * instructions based on Blackhole ISA.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tensix.h"
#include "tensix_cop.h"


/* Instruction implementation function pointer type.
 * Returns true if the instruction completed, false if blocked/incomplete.
 */
typedef bool (*insn_impl)(tensix_t *tt, uint32_t imm, int tid);

/* Forward declarations for all instruction implementations */
static bool unimp(tensix_t *tt, uint32_t imm, int tid);
static bool ttmop(tensix_t *tt, uint32_t imm, int tid);
static bool ttnop(tensix_t *tt, uint32_t imm, int tid);
static bool ttmop_cfg(tensix_t *tt, uint32_t imm, int tid);
static bool ttreplay(tensix_t *tt, uint32_t imm, int tid);
static bool ttresourcedecl(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovd2a(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovdbga2d(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovd2b(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovb2a(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovdbgb2d(tensix_t *tt, uint32_t imm, int tid);
static bool ttzeroacc(tensix_t *tt, uint32_t imm, int tid);
static bool ttzerosrc(tensix_t *tt, uint32_t imm, int tid);
static bool ttmova2d(tensix_t *tt, uint32_t imm, int tid);
static bool ttmovb2d(tensix_t *tt, uint32_t imm, int tid);
static bool tttrnspsrca(tensix_t *tt, uint32_t imm, int tid);
static bool ttrareb(tensix_t *tt, uint32_t imm, int tid);
static bool tttrnspsrcb(tensix_t *tt, uint32_t imm, int tid);
static bool ttshiftxa(tensix_t *tt, uint32_t imm, int tid);
static bool ttshiftxb(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetashrmh0(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetashrmh1(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetashrmv(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetpkedgof(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetashrmh(tensix_t *tt, uint32_t imm, int tid);
static bool ttclrexphist(tensix_t *tt, uint32_t imm, int tid);
static bool ttconv3s1(tensix_t *tt, uint32_t imm, int tid);
static bool ttconv3s2(tensix_t *tt, uint32_t imm, int tid);
static bool ttmpool3s1(tensix_t *tt, uint32_t imm, int tid);
static bool ttapool3s1(tensix_t *tt, uint32_t imm, int tid);
static bool ttmvmul(tensix_t *tt, uint32_t imm, int tid);
static bool ttelwmul(tensix_t *tt, uint32_t imm, int tid);
static bool ttelwadd(tensix_t *tt, uint32_t imm, int tid);
static bool ttdotpv(tensix_t *tt, uint32_t imm, int tid);
static bool ttelwsub(tensix_t *tt, uint32_t imm, int tid);
static bool ttmpool3s2(tensix_t *tt, uint32_t imm, int tid);
static bool ttapool3s2(tensix_t *tt, uint32_t imm, int tid);
static bool ttgmpool(tensix_t *tt, uint32_t imm, int tid);
static bool ttgapool(tensix_t *tt, uint32_t imm, int tid);
static bool ttgatesrcrst(tensix_t *tt, uint32_t imm, int tid);
static bool ttcleardvalid(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetrwc(tensix_t *tt, uint32_t imm, int tid);
static bool ttincrwc(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetibrwc(tensix_t *tt, uint32_t imm, int tid);
static bool ttmfconv3s1(tensix_t *tt, uint32_t imm, int tid);
static bool ttxmov(tensix_t *tt, uint32_t imm, int tid);
static bool ttpacr(tensix_t *tt, uint32_t imm, int tid);
static bool ttunpacr(tensix_t *tt, uint32_t imm, int tid);
static bool ttunpacr_nop(tensix_t *tt, uint32_t imm, int tid);
static bool ttrstdma(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetdmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttflushdma(tensix_t *tt, uint32_t imm, int tid);
static bool ttreg2flop(tensix_t *tt, uint32_t imm, int tid);
static bool ttloadind(tensix_t *tt, uint32_t imm, int tid);
static bool ttpacr_setreg(tensix_t *tt, uint32_t imm, int tid);
static bool tttbufcmd(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetadc(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetadcxy(tensix_t *tt, uint32_t imm, int tid);
static bool ttincadcxy(tensix_t *tt, uint32_t imm, int tid);
static bool ttaddrcrxy(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetadczw(tensix_t *tt, uint32_t imm, int tid);
static bool ttincadczw(tensix_t *tt, uint32_t imm, int tid);
static bool ttaddrcrzw(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetdvalid(tensix_t *tt, uint32_t imm, int tid);
static bool ttadddmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttsubdmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttmuldmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttbitwopdmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttshiftdmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttcmpdmareg(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetadcxx(tensix_t *tt, uint32_t imm, int tid);
static bool ttdmanop(tensix_t *tt, uint32_t imm, int tid);
static bool ttatincget(tensix_t *tt, uint32_t imm, int tid);
static bool ttatincgetptr(tensix_t *tt, uint32_t imm, int tid);
static bool ttatswap(tensix_t *tt, uint32_t imm, int tid);
static bool ttatcas(tensix_t *tt, uint32_t imm, int tid);
static bool ttstoreind(tensix_t *tt, uint32_t imm, int tid);
static bool ttstorereg(tensix_t *tt, uint32_t imm, int tid);
static bool ttloadreg(tensix_t *tt, uint32_t imm, int tid);
static bool sfpload(tensix_t *tt, uint32_t imm, int tid);
static bool sfploadi(tensix_t *tt, uint32_t imm, int tid);
static bool sfpstore(tensix_t *tt, uint32_t imm, int tid);
static bool sfplut(tensix_t *tt, uint32_t imm, int tid);
static bool sfpmuli(tensix_t *tt, uint32_t imm, int tid);
static bool sfpaddi(tensix_t *tt, uint32_t imm, int tid);
static bool sfpdivp2(tensix_t *tt, uint32_t imm, int tid);
static bool sfpexexp(tensix_t *tt, uint32_t imm, int tid);
static bool sfpexman(tensix_t *tt, uint32_t imm, int tid);
static bool sfpiadd(tensix_t *tt, uint32_t imm, int tid);
static bool sfpshft(tensix_t *tt, uint32_t imm, int tid);
static bool sfpsetcc(tensix_t *tt, uint32_t imm, int tid);
static bool sfpmov(tensix_t *tt, uint32_t imm, int tid);
static bool sfpabs(tensix_t *tt, uint32_t imm, int tid);
static bool sfpand(tensix_t *tt, uint32_t imm, int tid);
static bool sfpor(tensix_t *tt, uint32_t imm, int tid);
static bool sfpnot(tensix_t *tt, uint32_t imm, int tid);
static bool sfplz(tensix_t *tt, uint32_t imm, int tid);
static bool sfpsetexp(tensix_t *tt, uint32_t imm, int tid);
static bool sfpsetman(tensix_t *tt, uint32_t imm, int tid);
static bool sfpmad(tensix_t *tt, uint32_t imm, int tid);
static bool sfpadd(tensix_t *tt, uint32_t imm, int tid);
static bool sfpmul(tensix_t *tt, uint32_t imm, int tid);
static bool sfppushc(tensix_t *tt, uint32_t imm, int tid);
static bool sfppopc(tensix_t *tt, uint32_t imm, int tid);
static bool sfpsetsgn(tensix_t *tt, uint32_t imm, int tid);
static bool sfpencc(tensix_t *tt, uint32_t imm, int tid);
static bool sfpcompc(tensix_t *tt, uint32_t imm, int tid);
static bool sfptransp(tensix_t *tt, uint32_t imm, int tid);
static bool sfpxor(tensix_t *tt, uint32_t imm, int tid);
static bool sfp_stoch_rnd(tensix_t *tt, uint32_t imm, int tid);
static bool sfpnop(tensix_t *tt, uint32_t imm, int tid);
static bool sfpcast(tensix_t *tt, uint32_t imm, int tid);
static bool sfpconfig(tensix_t *tt, uint32_t imm, int tid);
static bool sfpswap(tensix_t *tt, uint32_t imm, int tid);
static bool sfploadmacro(tensix_t *tt, uint32_t imm, int tid);
static bool sfpshft2(tensix_t *tt, uint32_t imm, int tid);
static bool sfplutfp32(tensix_t *tt, uint32_t imm, int tid);
static bool sfple(tensix_t *tt, uint32_t imm, int tid);
static bool sfpgt(tensix_t *tt, uint32_t imm, int tid);
static bool sfpmul24(tensix_t *tt, uint32_t imm, int tid);
static bool sfparecip(tensix_t *tt, uint32_t imm, int tid);
static bool ttatgetm(tensix_t *tt, uint32_t imm, int tid);
static bool ttatrelm(tensix_t *tt, uint32_t imm, int tid);
static bool ttstallwait(tensix_t *tt, uint32_t imm, int tid);
static bool ttseminit(tensix_t *tt, uint32_t imm, int tid);
static bool ttsempost(tensix_t *tt, uint32_t imm, int tid);
static bool ttsemget(tensix_t *tt, uint32_t imm, int tid);
static bool ttsemwait(tensix_t *tt, uint32_t imm, int tid);
static bool ttstreamwait(tensix_t *tt, uint32_t imm, int tid);
static bool ttwrcfg(tensix_t *tt, uint32_t imm, int tid);
static bool ttrdcfg(tensix_t *tt, uint32_t imm, int tid);
static bool ttsetc16(tensix_t *tt, uint32_t imm, int tid);
static bool ttrmwcib0(tensix_t *tt, uint32_t imm, int tid);
static bool ttrmwcib1(tensix_t *tt, uint32_t imm, int tid);
static bool ttrmwcib2(tensix_t *tt, uint32_t imm, int tid);
static bool ttrmwcib3(tensix_t *tt, uint32_t imm, int tid);
static bool ttstreamwrcfg(tensix_t *tt, uint32_t imm, int tid);
static bool ttcfgshiftmask(tensix_t *tt, uint32_t imm, int tid);

/* Macro to simplify jump table creation */
#define OP(name) name

/* Instruction jump table - 256 entries (8-bit opcode) */
const insn_impl tt_jump_table[256] = {
    //  0x0          0x1          0x2          0x3          0x4          0x5          0x6          0x7          0x8          0x9          0xa          0xb          0xc          0xd          0xe          0xf
    OP(unimp),   OP(ttmop),   OP(ttnop),   OP(ttmop_cfg), OP(ttreplay), OP(ttresourcedecl), OP(unimp), OP(unimp), OP(ttmovd2a), OP(ttmovdbga2d), OP(ttmovd2b), OP(ttmovb2a), OP(ttmovdbgb2d), OP(unimp), OP(unimp), OP(unimp), // 0x00
    OP(ttzeroacc), OP(ttzerosrc), OP(ttmova2d), OP(ttmovb2d), OP(tttrnspsrca), OP(ttrareb), OP(tttrnspsrcb), OP(ttshiftxa), OP(ttshiftxb), OP(unimp), OP(ttsetashrmh0), OP(ttsetashrmh1), OP(ttsetashrmv), OP(ttsetpkedgof), OP(ttsetashrmh), OP(unimp), // 0x10
    OP(unimp), OP(ttclrexphist), OP(ttconv3s1), OP(ttconv3s2), OP(ttmpool3s1), OP(ttapool3s1), OP(ttmvmul), OP(ttelwmul), OP(ttelwadd), OP(ttdotpv), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0x20
    OP(ttelwsub), OP(ttmpool3s2), OP(ttapool3s2), OP(ttgmpool), OP(ttgapool), OP(ttgatesrcrst), OP(ttcleardvalid), OP(ttsetrwc), OP(ttincrwc), OP(ttsetibrwc), OP(ttmfconv3s1), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0x30
    OP(ttxmov), OP(ttpacr), OP(ttunpacr), OP(ttunpacr_nop), OP(ttrstdma), OP(ttsetdmareg), OP(ttflushdma), OP(unimp), OP(ttreg2flop), OP(ttloadind), OP(ttpacr_setreg), OP(tttbufcmd), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0x40
    OP(ttsetadc), OP(ttsetadcxy), OP(ttincadcxy), OP(ttaddrcrxy), OP(ttsetadczw), OP(ttincadczw), OP(ttaddrcrzw), OP(ttsetdvalid), OP(ttadddmareg), OP(ttsubdmareg), OP(ttmuldmareg), OP(ttbitwopdmareg), OP(ttshiftdmareg), OP(ttcmpdmareg), OP(ttsetadcxx), OP(unimp), // 0x50
    OP(ttdmanop), OP(ttatincget), OP(ttatincgetptr), OP(ttatswap), OP(ttatcas), OP(unimp), OP(ttstoreind), OP(ttstorereg), OP(ttloadreg), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0x60
    OP(sfpload), OP(sfploadi), OP(sfpstore), OP(sfplut), OP(sfpmuli), OP(sfpaddi), OP(sfpdivp2), OP(sfpexexp), OP(sfpexman), OP(sfpiadd), OP(sfpshft), OP(sfpsetcc), OP(sfpmov), OP(sfpabs), OP(sfpand), OP(sfpor), // 0x70
    OP(sfpnot), OP(sfplz), OP(sfpsetexp), OP(sfpsetman), OP(sfpmad), OP(sfpadd), OP(sfpmul), OP(sfppushc), OP(sfppopc), OP(sfpsetsgn), OP(sfpencc), OP(sfpcompc), OP(sfptransp), OP(sfpxor), OP(sfp_stoch_rnd), OP(sfpnop), // 0x80
    OP(sfpcast), OP(sfpconfig), OP(sfpswap), OP(sfploadmacro), OP(sfpshft2), OP(sfplutfp32), OP(sfple), OP(sfpgt), OP(sfpmul24), OP(sfparecip), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0x90
    OP(ttatgetm), OP(ttatrelm), OP(ttstallwait), OP(ttseminit), OP(ttsempost), OP(ttsemget), OP(ttsemwait), OP(ttstreamwait), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xa0
    OP(ttwrcfg), OP(ttrdcfg), OP(ttsetc16), OP(ttrmwcib0), OP(ttrmwcib1), OP(ttrmwcib2), OP(ttrmwcib3), OP(ttstreamwrcfg), OP(ttcfgshiftmask), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xb0
    OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xc0
    OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xd0
    OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xe0
    OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), OP(unimp), // 0xf0
};

/* Execute an instruction using the jump table.
 * Returns true if completed, false if blocked/incomplete.
 */
bool tensix_execute_insn(tensix_t *tt, uint32_t insn, int tid)
{
    uint8_t opcode = (insn >> 24) & 0xFF;
    uint32_t imm = insn & 0x00FFFFFF;

    return tt_jump_table[opcode](tt, imm, tid);
}

/*
 * Helper: Apply address modifier after math/unpack/pack operations.
 * mod_idx selects one of 8 addr_mod entries.
 *
 * Reads addr_mod config from ThreadConfig registers (thd_reg), set by SETC16:
 *   ADDR_MOD_AB:  thd_reg[12+i] - SrcA bits[5:0]=incr, bit6=cr, bit7=clr
 *                                  SrcB bits[13:8]=incr, bit14=cr, bit15=clr
 *   ADDR_MOD_DST: thd_reg[28+i] - bits[9:0]=incr, bit10=cr, bit11=clr,
 *                                  bit12=c_to_cr, bits[14:13]=fidelity_incr, bit15=fidelity_clr
 */
static void apply_addr_mod(tensix_t *tt, uint32_t mod_idx, int tid)
{
    if (mod_idx >= 8) return;

    uint32_t ab_reg  = tt->thd_reg[tid][12 + mod_idx];
    uint32_t dst_reg = tt->thd_reg[tid][28 + mod_idx];

    /* SrcA: increment, then handle clr/cr */
    uint32_t srca_incr = ab_reg & 0x3F;
    uint32_t srca_cr   = (ab_reg >> 6) & 1;
    uint32_t srca_clr  = (ab_reg >> 7) & 1;

    tt->srca_rwc[tid] += srca_incr;
    if (srca_clr) tt->srca_rwc[tid] = 0;
    if (srca_cr)  tt->srca_rwc[tid] = tt->srca_rwc_cr[tid];

    /* SrcB */
    uint32_t srcb_incr = (ab_reg >> 8) & 0x3F;
    uint32_t srcb_cr   = (ab_reg >> 14) & 1;
    uint32_t srcb_clr  = (ab_reg >> 15) & 1;

    tt->srcb_rwc[tid] += srcb_incr;
    if (srcb_clr) tt->srcb_rwc[tid] = 0;
    if (srcb_cr)  tt->srcb_rwc[tid] = tt->srcb_rwc_cr[tid];

    /* Dest: increment, snapshot to Cr if c_to_cr, then handle clr/cr */
    uint32_t dest_incr   = dst_reg & 0x3FF;
    uint32_t dest_cr     = (dst_reg >> 10) & 1;
    uint32_t dest_clr    = (dst_reg >> 11) & 1;
    uint32_t dest_c2cr   = (dst_reg >> 12) & 1;
    uint32_t fid_incr    = (dst_reg >> 13) & 0x3;
    uint32_t fid_clr     = (dst_reg >> 15) & 1;

    tt->dest_rwc[tid] += dest_incr;
    if (dest_c2cr) tt->dest_rwc_cr[tid] = tt->dest_rwc[tid];
    if (dest_clr) tt->dest_rwc[tid] = 0;
    if (dest_cr)  tt->dest_rwc[tid] = tt->dest_rwc_cr[tid];

    /* Fidelity */
    tt->fidelity[tid] += fid_incr;
    if (fid_clr) tt->fidelity[tid] = 0;
}

/*
 * Instruction implementations
 */

static bool unimp(tensix_t *tt, uint32_t imm, int tid) {
    (void)tt;
    (void)imm;
    /* Unimplemented instruction - do nothing */
    return true;
}

static bool ttnop(tensix_t *tt, uint32_t imm, int tid) {
    (void)tt;
    (void)imm;
    /* No operation */
    return true;
}

/* MOP_CFG (0x03): Configure MOP registers
 * Encoding: cfg_idx[23:20] | cfg_value[19:0]
 * cfg_idx 0-8: Set mop_cfg[idx] = cfg_value
 * cfg_idx 9: Set high 16 bits of zmask (zmask_hi16)
 */
static bool ttmop_cfg(tensix_t *tt, uint32_t imm, int tid) {
    if (!tt->cop) return true;

    uint32_t cfg_idx = (imm >> 20) & 0xF;
    uint32_t cfg_value = imm & 0xFFFFF;
    int thread_id = tid;

    if (cfg_idx < 9) {
        tt->cop->threads[thread_id].mop_cfg[cfg_idx] = cfg_value;
        TT_DBG("[MOP_CFG] thread=%d, cfg[%d] = 0x%x\n", thread_id, cfg_idx, cfg_value);
    } else if (cfg_idx == 9) {
        tt->cop->threads[thread_id].zmask_hi16 = cfg_value & 0xFFFF;
        TT_DBG("[MOP_CFG] thread=%d, zmask_hi16 = 0x%x\n", thread_id, cfg_value & 0xFFFF);
    }
    return true;
}

/* MOP (0x01): Execute Macro Operation
 * Encoding: template[23] | count1[22:16] | zmask_lo16[15:0] (for template 0)
 *           template[23] | loop_op1[22:0] (for template 1)
 */
static bool ttmop(tensix_t *tt, uint32_t imm, int tid) {
    if (!tt->cop) return true;

    int thread_id = tid;
    TT_DBG("[MOP] Expanding MOP, param=0x%06x, thread=%d\n", imm, thread_id);
    tensix_cop_mop_expand(tt->cop, thread_id, imm);
    return true;
}
static bool ttreplay(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttresourcedecl(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmovd2a(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmovdbga2d(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmovd2b(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmovb2a(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmovdbgb2d(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttzeroacc(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_ZEROACC(clear_mode, use_32_bit_mode, clear_zero_flags, addr_mode, where)
     * Encoding: clear_mode<<19 | use_32_bit_mode<<18 | clear_zero_flags<<17 | addr_mode<<14 | where<<0
     * clear_mode[1:0] = Mode: 0=one row, 1=16 rows, 2=half Dst, 3=all Dst
     */
    uint32_t clear_mode = (imm >> 19) & 0x1F;
    uint32_t use_32b    = (imm >> 18) & 0x1;
    /* uint32_t clear_zero_flags = (imm >> 17) & 0x1; */
    /* uint32_t addr_mode = (imm >> 14) & 0x7; */
    uint32_t where      = imm & 0x3FFF;

    uint32_t mode = clear_mode & 0x3;

    switch (mode) {
    case 0: /* ONE_ROW */
    {
        uint32_t row = where + tt->dest_rwc[tid];
        if (row < DEST_ROWS) {
            for (int j = 0; j < ROW_SIZE; j++)
                tt->dest[row][j] = 0.0f;
        }
        break;
    }
    case 1: /* 16_ROWS */
    {
        uint32_t base = where & 0xFF;
        uint32_t start = use_32b ? (base * 32) : (base * 16);
        for (uint32_t i = 0; i < 16 && (start + i) < DEST_ROWS; i++) {
            for (int j = 0; j < ROW_SIZE; j++)
                tt->dest[start + i][j] = 0.0f;
        }
        break;
    }
    case 2: /* HALF_OF_DST */
    {
        uint32_t start = (where & 0x1) ? 512 : 0;
        for (uint32_t i = start; i < start + 512 && i < DEST_ROWS; i++) {
            for (int j = 0; j < ROW_SIZE; j++)
                tt->dest[i][j] = 0.0f;
        }
        break;
    }
    case 3: /* ALL_OF_DST */
    default:
        for (int i = 0; i < DEST_ROWS; i++) {
            for (int j = 0; j < ROW_SIZE; j++)
                tt->dest[i][j] = 0.0f;
        }
        break;
    }
    return true;
}
static bool ttzerosrc(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_ZEROSRC(zero_val, write_mode, bank_mask, src_mask)
     * Encoding: zero_val<<4 | write_mode<<3 | bank_mask<<2 | src_mask<<0
     * zero_val (bool): 1 = fill SrcA with negative infinity instead of zero
     * write_mode (bool): SingleBankMatrixUnit - select bank by MatrixUnit side
     * bank_mask (bool): BothBanks - clear both banks
     * src_mask[1:0]: bit0=ClearSrcA, bit1=ClearSrcB
     */
    uint32_t neg_inf    = (imm >> 4) & 0x1;
    /* uint32_t write_mode = (imm >> 3) & 0x1; */
    /* uint32_t bank_mask  = (imm >> 2) & 0x1; */
    uint32_t src_mask   = imm & 0x3;

    /* Simplified: always clear both banks (emulator has single-bank arrays) */
    if (src_mask & 0x1) {  /* ClearSrcA */
        float fill = neg_inf ? tt->neginf : 0.0f;
        for (int i = 0; i < SRCA_ROWS; i++) {
            for (int j = 0; j < ROW_SIZE; j++) {
                tt->srca[i][j] = fill;
            }
        }
    }
    if (src_mask & 0x2) {  /* ClearSrcB (always zero, never neg_inf) */
        for (int i = 0; i < SRCB_ROWS; i++) {
            for (int j = 0; j < ROW_SIZE; j++) {
                tt->srcb[i][j] = 0.0f;
            }
        }
    }
    return true;
}
static bool ttmova2d(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_MOVA2D(dest_32b_lo, src, addr_mode, instr_mod, dst)
     * Encoding: dest_32b_lo<<23 | src<<17 | addr_mode<<14 | instr_mod<<12 | dst<<0
     *
     * ISA (MOVA2D.md): Move 1 or 8 rows from SrcA to Dest.
     *   dest_32b_lo: write to low 16 bits of Dst32b (rare usage)
     *   src[5:0]: SrcRow base
     *   addr_mode[2:0]: AddrMod index
     *   instr_mod[1:0] (bits[13:12]): ISA syntax (Move8Rows)<<1, so Move8Rows = instr_mod>>1
     *   dst[11:0]: DstRow base
     */
    uint32_t addr_mode  = (imm >> 14) & 0x7;
    uint32_t instr_mod  = (imm >> 12) & 0x3;
    uint32_t move8rows  = (instr_mod >> 1) & 0x1;
    uint32_t src_row    = (imm >> 17) & 0x3F;
    uint32_t dst_row    = imm & 0xFFF;

    /* Determine row range.
     * ISA: DstRow += ThreadConfig.DEST_TARGET_REG_CFG_MATH_Offset (thd_reg[1])
     *      DstRow += RWCs.Dst + ConfigState.DEST_REGW_BASE_Base
     * We use thd_reg[thread_id][1] for the Math Offset set by SETC16.
     */
    unsigned num_rows;
    uint32_t math_offset = tt->thd_reg[tid][1];
    uint32_t srca_row = src_row + tt->srca_rwc[tid];
    uint32_t dest_base = dst_row + math_offset + tt->dest_rwc[tid];
    if (move8rows) {
        num_rows = 8;
        dest_base &= 0x3F8;
        srca_row &= 0x38;
    } else {
        num_rows = 1;
        dest_base &= 0x3FF;
        srca_row &= 0x3F;
    }

    /* Copy SrcA rows to Dest (simplified: BF16-as-float passthrough) */
    for (unsigned i = 0; i < num_rows; i++) {
        uint32_t sa_idx = srca_row + i;
        uint32_t d_idx  = dest_base + i;
        if (sa_idx >= SRCA_ROWS || d_idx >= DEST_ROWS)
            continue;
        for (int j = 0; j < ROW_SIZE; j++) {
            tt->dest[d_idx][j] = tt->srca[sa_idx][j];
        }
    }

    TT_DBG("[MOVA2D] srca_row=%d, dest_base=%d, num_rows=%d\n",
           srca_row, dest_base, num_rows);

    /* Note: srca_dvalid is NOT cleared here. It is cleared by SETRWC
     * (clear_ab_vld) which appears as end_op0 in the MOP template. */

    apply_addr_mod(tt, addr_mode, tid);
    return true;
}
static bool ttmovb2d(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool tttrnspsrca(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttrareb(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool tttrnspsrcb(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttshiftxa(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttshiftxb(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetashrmh0(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetashrmh1(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetashrmv(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetpkedgof(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetashrmh(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttclrexphist(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttconv3s1(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttconv3s2(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmpool3s1(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttapool3s1(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmvmul(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttelwmul(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttelwadd(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_ELWADD(clear_dvalid, dest_accum_en, instr_mod19, addr_mode, dst)
     * Encoding: clear_dvalid<<22 | dest_accum_en<<21 | instr_mod19<<19 | addr_mode<<14 | dst<<0
     *
     * ISA functional model (ELWADD.md):
     *   An aligned 8x16 block of SrcA is added element-wise to an aligned
     *   8x16 block of SrcB, result assigned to or accumulated into Dst.
     *
     *   clear_dvalid[1:0]: bit0=FlipSrcA, bit1=FlipSrcB
     *   dest_accum_en: AddDst (accumulate with existing Dst value)
     *   instr_mod19[1:0]: bit1=BroadcastSrcBRow, bit0=BroadcastSrcBCol0
     *   addr_mode[2:0]: AddrMod index (lower 3 bits)
     *   dst[13:0]: DstRow base
     *
     *   SrcARow = RWC.SrcA & 0x38 (aligned to 8)
     *   SrcBRow = RWC.SrcB & (BroadcastSrcBRow ? 0x3F : 0x38)
     *   DstRow = (dst + RWC.Dst + DestOffset) & 0x3F8 (aligned to 8)
     *   For i in 0..7, j in 0..15:
     *     Result = SrcA[SrcARow+i][j] + SrcB[SrcBRow+(bcast_row?0:i)][bcast_col0?0:j]
     *     if (AddDst) Result += Dst[DstRow+i][j]
     *     Dst[DstRow+i][j] = Result
     *   FlipSrcA/B: release src bank back to unpacker
     *   ApplyAddrMod(AddrMod)
     */
    uint32_t clear_dvalid = (imm >> 22) & 0x3;
    uint32_t dest_accum   = (imm >> 21) & 0x1;
    uint32_t instr_mod19  = (imm >> 19) & 0x3;
    uint32_t addr_mode    = (imm >> 14) & 0x7;
    uint32_t dst_row      = imm & 0x3FFF;

    bool flip_srca       = clear_dvalid & 0x1;
    bool flip_srcb       = (clear_dvalid >> 1) & 0x1;
    bool bcast_srcb_row  = (instr_mod19 >> 1) & 0x1;
    bool bcast_srcb_col0 = instr_mod19 & 0x1;

    /* Determine row indices */
    uint32_t srca_row = tt->srca_rwc[tid] & 0x38;
    uint32_t srcb_row = tt->srcb_rwc[tid] & (bcast_srcb_row ? 0x3F : 0x38);
    uint32_t dest_base = (dst_row + tt->dest_rwc[tid] + tt->dest_offset) & 0x3F8;

    /* Element-wise add: 8 rows x 16 columns */
    TT_DBG("[ELWADD] srca_row=%d, srcb_row=%d, dest_base=%d, srca_dvalid=%d, srcb_dvalid=%d\n",
           srca_row, srcb_row, dest_base, tt->srca_dvalid, tt->srcb_dvalid);
    TT_DBG("[ELWADD] SrcA[%d][0..3] = %f %f %f %f\n", srca_row,
           tt->srca[srca_row][0], tt->srca[srca_row][1], tt->srca[srca_row][2], tt->srca[srca_row][3]);
    TT_DBG("[ELWADD] SrcB[%d][0..3] = %f %f %f %f\n", srcb_row,
           tt->srcb[srcb_row][0], tt->srcb[srcb_row][1], tt->srcb[srcb_row][2], tt->srcb[srcb_row][3]);
    for (int i = 0; i < 8; i++) {
        uint32_t sa_idx = srca_row + i;
        uint32_t sb_idx = srcb_row + (bcast_srcb_row ? 0 : i);
        uint32_t d_idx  = dest_base + i;

        if (sa_idx >= SRCA_ROWS || sb_idx >= SRCB_ROWS || d_idx >= DEST_ROWS)
            continue;

        for (int j = 0; j < ROW_SIZE; j++) {
            float a_val = tt->srca[sa_idx][j];
            float b_val = tt->srcb[sb_idx][bcast_srcb_col0 ? 0 : j];
            float result = a_val + b_val;

            if (dest_accum)
                result += tt->dest[d_idx][j];

            tt->dest[d_idx][j] = result;
        }
    }
    TT_DBG("[ELWADD] Dest[%d][0..3] = %f %f %f %f\n", dest_base,
           tt->dest[dest_base][0], tt->dest[dest_base][1], tt->dest[dest_base][2], tt->dest[dest_base][3]);
    {
        uint32_t _b[4];
        for (int _k = 0; _k < 4; _k++) memcpy(&_b[_k], &tt->dest[dest_base][_k], 4);
        TT_DBG("[ELWADD] Dest[%d][0..3] hex = 0x%08x 0x%08x 0x%08x 0x%08x\n",
               dest_base, _b[0], _b[1], _b[2], _b[3]);
        TT_DBG("[ELWADD] Dest[%d][0..3] bf16 = 0x%04x 0x%04x 0x%04x 0x%04x\n",
               dest_base, _b[0] >> 16, _b[1] >> 16, _b[2] >> 16, _b[3] >> 16);
    }

    /* Note: dest_dvalid is NOT set here. It is set by SEMPOST from MATH core
     * (thread_id==1) after all 8 ELWADDs in the MOP complete.  Setting it here
     * would allow PACR to start after the very first ELWADD, reading Dest rows
     * that haven't been computed yet.
     */

    /* Flip source banks (release to unpacker) */
    if (flip_srca) tt->srca_dvalid = false;
    if (flip_srcb) tt->srcb_dvalid = false;

    /* Apply address modifier */
    apply_addr_mod(tt, addr_mode, tid);
    return true;
}
static bool ttdotpv(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttelwsub(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmpool3s2(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttapool3s2(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttgmpool(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttgapool(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttgatesrcrst(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttcleardvalid(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_CLEARDVALID(cleardvalid, reset)
     * Encoding: cleardvalid<<22 | reset<<0
     * cleardvalid[1:0]: bit0=FlipSrcA, bit1=FlipSrcB
     * reset[1:0]: bit0=Reset (reset all bank state), bit1=KeepReadingSameSrc
     *
     * ISA functional model:
     *   if (Reset) clear all dvalid and reset banks to 0
     *   else selectively flip SrcA/SrcB banks and give to unpackers
     */
    uint32_t flip      = (imm >> 22) & 0x3;
    uint32_t reset     = imm & 0x1;
    /* uint32_t keep_same = (imm >> 1) & 0x1; */

    if (reset) {
        tt->srca_dvalid = false;
        tt->srcb_dvalid = false;
        tt->dest_dvalid = false;
    } else {
        if (flip & 0x1)  /* FlipSrcA */
            tt->srca_dvalid = false;
        if (flip & 0x2)  /* FlipSrcB */
            tt->srcb_dvalid = false;
    }
    return true;
}
static bool ttsetrwc(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETRWC(clear_ab_vld, rwc_cr, rwc_d, rwc_b, rwc_a, BitMask)
     * Encoding: clear_ab_vld<<22 | rwc_cr<<18 | rwc_d<<14 | rwc_b<<10 | rwc_a<<6 | BitMask<<0
     *
     * BitMask[3:0]: bit0=SrcA, bit1=SrcB, bit2=Dst, bit3=Fidelity
     * rwc_cr[3:0]: bit0=SrcACr, bit1=SrcBCr, bit2=DstCr, bit3=DstCtoCr
     * clear_ab_vld[1:0]: bit0=FlipSrcA, bit1=FlipSrcB
     *
     * ISA: if BitMask.SrcA set, RWC.SrcA = rwc_a (+SrcA_Cr if SrcACr)
     *      if BitMask.SrcB set, RWC.SrcB = rwc_b (+SrcB_Cr if SrcBCr)
     *      if BitMask.Dst set,  RWC.Dst  = rwc_d (+Dst_Cr if DstCr, +Dst if DstCtoCr)
     *      if BitMask.Fidelity, RWC.FidelityPhase = 0
     */
    uint32_t clear_ab_vld = (imm >> 22) & 0x3;
    uint32_t rwc_cr       = (imm >> 18) & 0xF;
    uint32_t rwc_d        = (imm >> 14) & 0xF;
    uint32_t rwc_b        = (imm >> 10) & 0xF;
    uint32_t rwc_a        = (imm >>  6) & 0xF;
    uint32_t bitmask      = imm & 0x3F;

    if (bitmask & 0x1) {  /* SrcA */
        uint32_t val = rwc_a;
        if (rwc_cr & 0x1)  /* SrcACr */
            val += tt->srca_rwc_cr[tid];
        tt->srca_rwc[tid] = val;
        tt->srca_rwc_cr[tid] = val;
    }
    if (bitmask & 0x2) {  /* SrcB */
        uint32_t val = rwc_b;
        if (rwc_cr & 0x2)  /* SrcBCr */
            val += tt->srcb_rwc_cr[tid];
        tt->srcb_rwc[tid] = val;
        tt->srcb_rwc_cr[tid] = val;
    }
    if (bitmask & 0x4) {  /* Dst */
        uint32_t val = rwc_d;
        if (rwc_cr & 0x8)       /* DstCtoCr */
            val += tt->dest_rwc[tid];
        else if (rwc_cr & 0x4)  /* DstCr */
            val += tt->dest_rwc_cr[tid];
        tt->dest_rwc[tid] = val;
        tt->dest_rwc_cr[tid] = val;
    }
    if (bitmask & 0x8) {  /* Fidelity */
        tt->fidelity[tid] = 0;
    }

    /* FlipSrcA / FlipSrcB */
    if (clear_ab_vld & 0x1)
        tt->srca_dvalid = false;
    if (clear_ab_vld & 0x2)
        tt->srcb_dvalid = false;
    return true;
}
static bool ttincrwc(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_INCRWC(rwc_cr, rwc_d, rwc_b, rwc_a)
     * Encoding: rwc_cr<<18 | rwc_d<<14 | rwc_b<<10 | rwc_a<<6
     *
     * rwc_cr[2:0]: bit0=SrcACr, bit1=SrcBCr, bit2=DstCr
     * rwc_a: 4-bit increment for SrcA
     * rwc_b: 4-bit increment for SrcB
     * rwc_d: 4-bit increment for Dst
     *
     * ISA: All three counters are always incremented simultaneously.
     *      If Cr bit set, increment the Cr counter and copy to main counter.
     *      Otherwise, increment the main counter only.
     */
    uint32_t rwc_cr = (imm >> 18) & 0x7;
    uint32_t rwc_d  = (imm >> 14) & 0xF;
    uint32_t rwc_b  = (imm >> 10) & 0xF;
    uint32_t rwc_a  = (imm >>  6) & 0xF;

    if (rwc_cr & 0x1) {  /* SrcACr */
        tt->srca_rwc_cr[tid] += rwc_a;
        tt->srca_rwc[tid] = tt->srca_rwc_cr[tid];
    } else {
        tt->srca_rwc[tid] += rwc_a;
    }

    if (rwc_cr & 0x2) {  /* SrcBCr */
        tt->srcb_rwc_cr[tid] += rwc_b;
        tt->srcb_rwc[tid] = tt->srcb_rwc_cr[tid];
    } else {
        tt->srcb_rwc[tid] += rwc_b;
    }

    if (rwc_cr & 0x4) {  /* DstCr */
        tt->dest_rwc_cr[tid] += rwc_d;
        tt->dest_rwc[tid] = tt->dest_rwc_cr[tid];
    } else {
        tt->dest_rwc[tid] += rwc_d;
    }
    return true;
}
static bool ttsetibrwc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmfconv3s1(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttxmov(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }

/*
 * Data format conversion helpers for UNPACR/PACR
 */

/* Get datum size in bytes for a data format code */
static uint32_t get_datum_size(uint32_t fmt)
{
    switch (fmt) {
    case 0:  /* Float32 */
    case 4:  /* Tf32 */
    case 8:  /* Int32 */
        return 4;
    case 1:  /* Float16 */
    case 5:  /* Float16_b (BF16) */
    case 9:  /* Int16 */
    case 12: /* UInt16 */
        return 2;
    case 10: /* Lf8 */
    case 14: /* Int8 */
    case 30: /* UInt8 */
        return 1;
    case 2:  /* Bfp8 */
    case 6:  /* Bfp8_b */
        return 1;
    case 3:  /* Bfp4 */
    case 7:  /* Bfp4_b */
    case 11: /* Bfp2 */
    case 15: /* Bfp2_b */
        return 1; /* approximate for sub-byte formats */
    default:
        return 4;
    }
}

/* Convert raw L1 datum bits to float */
static float datum_to_float(uint32_t bits, uint32_t fmt)
{
    switch (fmt) {
    case 0: /* Float32 */
    case 4: /* Tf32 */ {
        float f;
        memcpy(&f, &bits, sizeof(f));
        return f;
    }
    case 5: /* Float16_b (BF16) */ {
        uint32_t fp32_bits = bits << 16;
        float f;
        memcpy(&f, &fp32_bits, sizeof(f));
        return f;
    }
    case 1: /* Float16 */ {
        uint32_t sign = (bits >> 15) & 1;
        uint32_t exp = (bits >> 10) & 0x1F;
        uint32_t mant = bits & 0x3FF;
        uint32_t fp32_bits;
        if (exp == 0) {
            if (mant == 0) {
                fp32_bits = sign << 31;
            } else {
                /* Denormalized FP16 */
                exp = 1;
                while (!(mant & 0x400)) { mant <<= 1; exp--; }
                mant &= 0x3FF;
                fp32_bits = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
            }
        } else if (exp == 31) {
            fp32_bits = (sign << 31) | 0x7F800000 | (mant << 13);
        } else {
            fp32_bits = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
        }
        float f;
        memcpy(&f, &fp32_bits, sizeof(f));
        return f;
    }
    case 8: /* Int32 */
        return (float)(int32_t)bits;
    case 9: /* Int16 */
        return (float)(int16_t)(uint16_t)bits;
    case 12: /* UInt16 */
        return (float)(uint16_t)bits;
    case 14: /* Int8 */
        return (float)(int8_t)(uint8_t)bits;
    case 30: /* UInt8 */
        return (float)(uint8_t)bits;
    default:
        return 0.0f;
    }
}

/* Convert float to raw datum bits for L1 storage */
static uint32_t float_to_datum(float f, uint32_t fmt)
{
    switch (fmt) {
    case 0: /* Float32 */
    case 4: /* Tf32 */ {
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        return bits;
    }
    case 5: /* Float16_b (BF16) */ {
        uint32_t fp32_bits;
        memcpy(&fp32_bits, &f, sizeof(fp32_bits));
        return (fp32_bits >> 16) & 0xFFFF;
    }
    case 1: /* Float16 */ {
        uint32_t fp32_bits;
        memcpy(&fp32_bits, &f, sizeof(fp32_bits));
        uint32_t sign = (fp32_bits >> 31) & 1;
        int32_t exp = (int32_t)((fp32_bits >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = (fp32_bits >> 13) & 0x3FF;
        if (((fp32_bits >> 23) & 0xFF) == 0) return sign << 15; /* zero/denorm */
        if (((fp32_bits >> 23) & 0xFF) == 0xFF) return (sign << 15) | 0x7C00 | mant;
        if (exp <= 0) return sign << 15;
        if (exp >= 31) return (sign << 15) | 0x7C00;
        return (sign << 15) | ((uint32_t)exp << 10) | mant;
    }
    case 8: /* Int32 */
        return (uint32_t)(int32_t)f;
    case 9: /* Int16 */
        return (uint32_t)(uint16_t)(int16_t)f;
    case 12: /* UInt16 */
        return (uint32_t)(uint16_t)f;
    case 14: /* Int8 */
        return (uint32_t)(uint8_t)(int8_t)f;
    case 30: /* UInt8 */
        return (uint32_t)(uint8_t)f;
    default:
        return 0;
    }
}

static bool ttpacr(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_PACR(CfgContext, RowPadZero, DstAccessMode, AddrMode,
     *   AddrCntContext, ZeroWrite, ReadIntfSel, OvrdThreadId, Concat, CtxtCtrl,
     *   Flush, Last)
     * Encoding:
     *   CfgContext<<21 | RowPadZero<<18 | DstAccessMode<<17 | AddrMode<<15 |
     *   AddrCntContext<<13 | ZeroWrite<<12 | ReadIntfSel<<8 | OvrdThreadId<<7 |
     *   Concat<<4 | CtxtCtrl<<2 | Flush<<1 | Last<<0
     *
     * ISA (PACR.md): Move datums from Dst to L1.
     */
    /* uint32_t cfg_ctx      = (imm >> 21) & 0x7; */
    /* uint32_t row_pad_zero = (imm >> 18) & 0x7; */
    /* uint32_t dst_access   = (imm >> 17) & 0x1; */
    uint32_t addr_mode       = (imm >> 15) & 0x3;
    /* uint32_t adc_ctx      = (imm >> 13) & 0x3; */
    uint32_t zero_write      = (imm >> 12) & 0x1;
    uint32_t packer_mask     = (imm >> 8) & 0xF;
    /* uint32_t ovrd_thread  = (imm >> 7) & 0x1; */
    /* uint32_t concat       = (imm >> 4) & 0x7; */
    /* uint32_t ctx_ctrl     = (imm >> 2) & 0x3; */
    uint32_t flush           = (imm >> 1) & 0x1;
    /* uint32_t last         = imm & 0x1; */

    /* PackerMask 0b0000 is rewritten to 0b0001 */
    if (packer_mask == 0) packer_mask = 1;

    uint32_t state_id = tt->thd_reg[tid][0] & 0x1;
    AddrCtrl *adc = &tt->adc[2]; /* Packer ADC */

    /*
     * Packer input address generator (from Dest):
     *   PCK0_ADDR_BASE_REG_0_Base          @ cfg_reg[16] bits[17:0]
     *   PCK0_ADDR_CTRL_XY_REG_0_Xstride    @ cfg_reg[12] bits[15:0]
     *   PCK0_ADDR_CTRL_XY_REG_0_Ystride    @ cfg_reg[12] bits[31:16]
     *   PCK0_ADDR_CTRL_ZW_REG_0_Zstride    @ cfg_reg[13] bits[15:0]
     *   PCK0_ADDR_CTRL_ZW_REG_0_Wstride    @ cfg_reg[13] bits[31:16]
     *
     * Packer data format:
     *   THCON_SEC0_REG1_In_data_format     @ cfg_reg[70] bits[11:8]
     *   THCON_SEC0_REG1_Out_data_format    @ cfg_reg[70] bits[3:0]
     *
     * Packer output address generator (to L1):
     *   THCON_SEC0_REG1_L1_Dest_addr       @ cfg_reg[69] (full 32-bit)
     *   PCK0_ADDR_BASE_REG_1_Base          @ cfg_reg[17] bits[17:0]
     *   PCK0_ADDR_CTRL_XY_REG_1_Ystride    @ cfg_reg[14] bits[31:16]
     *   PCK0_ADDR_CTRL_ZW_REG_1_Zstride    @ cfg_reg[15] bits[15:0]
     *   PCK0_ADDR_CTRL_ZW_REG_1_Wstride    @ cfg_reg[15] bits[31:16]
     *
     * DEST_TARGET_REG_CFG_PACK_SEC[i]_Offset @ cfg_reg[180+i] bits[11:0]
     */

    /* Input format for datum size computation - read from high_mem */
    uint32_t in_data_fmt = (tensix_read_cfg(&tt->mem, 70) >> 8) & 0xF;
    uint32_t out_data_fmt = tensix_read_cfg(&tt->mem, 70) & 0xF;
    uint32_t out_dsb = get_datum_size(out_data_fmt);

    /* Bytes per datum from In_data_format (for Dest address computation) */
    uint32_t bytes_per_datum, adc_x_mask;
    switch (in_data_fmt & 3) {
    case 0:  bytes_per_datum = 4; adc_x_mask = 0x3; break;
    case 1:  bytes_per_datum = 2; adc_x_mask = 0x7; break;
    default: bytes_per_datum = 1; adc_x_mask = 0xf; break;
    }

    /* Packer input base address and strides - read from high_mem */
    uint32_t pck_base_in = tensix_read_cfg(&tt->mem, 16) & 0x3FFFF;
    uint32_t pck_xstride_in = tensix_read_cfg(&tt->mem, 12) & 0xFFFF;
    uint32_t pck_ystride_in = (tensix_read_cfg(&tt->mem, 12) >> 16) & 0xFFFF;
    uint32_t pck_zstride_in = tensix_read_cfg(&tt->mem, 13) & 0xFFFF;
    uint32_t pck_wstride_in = (tensix_read_cfg(&tt->mem, 13) >> 16) & 0xFFFF;

    /* L1 output address: L1_Dest_addr + 1 (skip tile header) */
    uint32_t cfg69_raw = tensix_read_cfg(&tt->mem, 69);
    uint32_t l1_dest_addr = cfg69_raw + 1;

    /* Detect new tile: reset running write offset when cfg[69] changes
     * (program_packer_destination writes new address per tile) */
    if (cfg69_raw != tt->pack_l1_dest_addr_raw) {
        tt->pack_l1_dest_addr_raw = cfg69_raw;
        tt->pack_l1_write_offset = 0;
    }

    /* L1 byte base address (skip 16-byte tile header) */
    uint32_t l1_base_byte_addr = (l1_dest_addr & 0x1FFFF) << 4;

    /* Process each active packer */
    for (int pi = 0; pi < 4; pi++) {
        if (!((packer_mask >> pi) & 1)) continue;

        /* Compute Dest input address */
        uint32_t addr = pck_base_in
            + adc->ch0_x * (pck_xstride_in & 0xF)
            + adc->ch0_y * pck_ystride_in
            + adc->ch0_z * pck_zstride_in
            + adc->ch0_w * pck_wstride_in;

        /* Convert byte address to datum address */
        addr = ((addr / bytes_per_datum) & ~adc_x_mask) + (adc->ch0_x & adc_x_mask);

        /* Add DEST_TARGET_REG_CFG_PACK_SEC[pi].Offset << 4 */
        uint32_t dest_offset = 0;
        if (180 + (uint32_t)pi < CFG_REG_COUNT) {
            dest_offset = tensix_read_cfg(&tt->mem, 180 + pi) & 0xFFF;
        }
        addr += dest_offset << 4;

        uint32_t src_addr = addr & 0x3FFF;

        /* Number of datums to pack per PACR call.
         * Hardware packs a 2D block: x_count columns × y_count rows.
         * y_count comes from ysrc_incr in addr_mod[0] (thd_reg[37]),
         * which represents the packer's row count per call regardless of
         * which addr_mode the current PACR instruction uses.
         */
        uint32_t num_datums;
        if (flush) {
            num_datums = 0;
        } else {
            uint32_t x_count = (adc->ch1_x >= adc->ch0_x) ? (adc->ch1_x - adc->ch0_x + 1) : 0;
            uint32_t pack_reg0 = tt->thd_reg[tid][37]; /* always addr_mod[0] */
            uint32_t ysrc_incr0 = pack_reg0 & 0xF;
            uint32_t y_count = (ysrc_incr0 > 0) ? ysrc_incr0 : 1;
            num_datums = x_count * y_count;
        }
        if (num_datums > 1024) num_datums = 1024;

        /* Compute L1 output byte address.
         * In normal pack mode the firmware does not configure CH1 output strides
         * (cfg[14]/[15]).  The hardware writes datums sequentially from l1_dest_addr.
         * We track a running byte offset (pack_l1_write_offset) that advances by
         * num_datums * out_dsb after each PACR call.
         */
        uint32_t l1_byte_addr = l1_base_byte_addr + tt->pack_l1_write_offset;

        if (tt->mem.l1_scratchpad == NULL || num_datums == 0) continue;

        TT_DBG("[PACR] packer=%d, src_addr=%d, num_datums=%d, l1_addr=0x%x, out_fmt=%d\n",
               pi, src_addr, num_datums, l1_byte_addr, out_data_fmt);
        TT_DBG("[PACR] Dest[%d][0..3] = %f %f %f %f\n", src_addr >> 4,
               tt->dest[src_addr >> 4][0], tt->dest[src_addr >> 4][1],
               tt->dest[src_addr >> 4][2], tt->dest[src_addr >> 4][3]);

        /* Read 2D block from Dest (y_count rows × x_count cols).
         * ystride_datums is the Dest row stride in datum units (from pck_ystride_in).
         * Output to L1 is sequential (row after row, no gaps).
         */
        /* Dest is 16 columns wide; one row = 16 datums */
        uint32_t ystride_datums = 16;

        uint32_t pack_reg0 = tt->thd_reg[tid][37];
        uint32_t ysrc_incr0 = pack_reg0 & 0xF;
        uint32_t y_count = (ysrc_incr0 > 0) ? ysrc_incr0 : 1;
        uint32_t x_count = (num_datums > 0) ? num_datums / y_count : 0;

        uint32_t l1_idx = 0;
        for (uint32_t row = 0; row < y_count; row++) {
            for (uint32_t col = 0; col < x_count; col++) {
                uint32_t d_idx = src_addr + row * ystride_datums + col;
                uint32_t d_row = (d_idx >> 4) & 0x3FF;
                uint32_t d_col = d_idx & 0xF;

                float val = 0.0f;
                if (!zero_write && d_row < DEST_ROWS) {
                    val = tt->dest[d_row][d_col];
                }

                uint32_t raw = float_to_datum(val, in_data_fmt);
                uint32_t l1_off = l1_byte_addr + l1_idx * out_dsb;
                if (l1_off + out_dsb <= 0x180000) {
                    memcpy(tt->mem.l1_scratchpad + l1_off, &raw, out_dsb);
                    if (l1_idx < 4) TT_DBG("[PACR]   Dest[%d][%d]=%f -> L1[0x%x]=0x%x\n", d_row, d_col, val, l1_off, raw);
                }
                l1_idx++;
            }
        }

        /* Advance running L1 write offset for next PACR */
        tt->pack_l1_write_offset += num_datums * out_dsb;
    }

    /* Apply packer address modifier from ThreadConfig thd_reg[37+i]
     * ADDR_MOD_PACK_SEC[i]: bits[3:0]=Y_src_incr, bit4=Y_src_cr, bit5=Y_src_clr,
     *   bits[9:6]=Y_dst_incr, bit10=Y_dst_cr, bit11=Y_dst_clr,
     *   bit12=Z_src_incr, bit13=Z_src_clr, bit14=Z_dst_incr, bit15=Z_dst_clr
     */
    uint32_t pack_reg = (addr_mode < 4) ? tt->thd_reg[tid][37 + addr_mode] : 0;

    uint32_t ysrc_incr = pack_reg & 0xF;
    uint32_t ysrc_cr   = (pack_reg >> 4) & 1;
    uint32_t ysrc_clr  = (pack_reg >> 5) & 1;
    uint32_t ydst_incr = (pack_reg >> 6) & 0xF;
    uint32_t ydst_cr   = (pack_reg >> 10) & 1;
    uint32_t ydst_clr  = (pack_reg >> 11) & 1;
    uint32_t zsrc_incr = (pack_reg >> 12) & 1;
    uint32_t zsrc_clr  = (pack_reg >> 13) & 1;
    uint32_t zdst_incr = (pack_reg >> 14) & 1;
    uint32_t zdst_clr  = (pack_reg >> 15) & 1;

    /* Y source counter update */
    adc->ch0_y += ysrc_incr;
    if (ysrc_clr) adc->ch0_y = 0;
    if (ysrc_cr)  adc->ch0_y = adc->ch0_y_cr;

    /* Z source counter update */
    adc->ch0_z += zsrc_incr;
    if (zsrc_clr) adc->ch0_z = 0;

    /* Y dest counter update */
    adc->ch1_y += ydst_incr;
    if (ydst_clr) adc->ch1_y = 0;
    if (ydst_cr)  adc->ch1_y = adc->ch1_y_cr;

    /* Z dest counter update */
    adc->ch1_z += zdst_incr;
    if (zdst_clr) adc->ch1_z = 0;

    return true;
}
static bool ttunpacr(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_UNPACR(Unpack_block_selection, AddrMode,
     *   CfgContextCntInc, CfgContextId, AddrCntContextId, OvrdThreadId,
     *   SetDatValid, srcb_bcast, ZeroWrite2, AutoIncContextID,
     *   RowSearch, SearchCacheFlush, Last)
     * Encoding:
     *   Unpack_block_selection<<23 | AddrMode<<15 | CfgContextCntInc<<13 |
     *   CfgContextId<<10 | AddrCntContextId<<8 | OvrdThreadId<<7 |
     *   SetDatValid<<6 | srcb_bcast<<5 | ZeroWrite2<<4 |
     *   AutoIncContextID<<3 | RowSearch<<2 | SearchCacheFlush<<1 | Last<<0
     *
     * ISA (UNPACR.md): Move datums from L1 to SrcA or SrcB.
     */
    uint32_t which_unp        = (imm >> 23) & 0x1;
    uint32_t addr_mode_field  = (imm >> 15) & 0xFF;
    uint32_t cfg_ctx_cnt_inc  = (imm >> 13) & 0x3;
    /* uint32_t cfg_ctx_id    = (imm >> 10) & 0x7; */
    /* uint32_t adc_ctx_id    = (imm >> 8) & 0x3; */
    uint32_t multi_ctx_mode   = (imm >> 7) & 0x1;
    uint32_t set_dat_valid    = (imm >> 6) & 0x1;
    /* uint32_t srcb_bcast    = (imm >> 5) & 0x1; */
    uint32_t zero_write       = (imm >> 4) & 0x1;
    /* uint32_t auto_inc_ctx  = (imm >> 3) & 0x1; */
    /* uint32_t row_search    = (imm >> 2) & 0x1; */
    uint32_t cache_flush      = (imm >> 1) & 0x1;
    /* uint32_t last          = imm & 0x1; */

    /* Sub-mode: FlushCache */
    if (cache_flush) {
        return true;
    }

    /* Sub-mode: IncrementContextCounter */
    if (cfg_ctx_cnt_inc) {
        return true;
    }

    /*
     * Regular unpack mode: read tile data from L1, convert format,
     * write to SrcA (which_unp=0) or SrcB (which_unp=1).
     *
     * Config register layout (Blackhole):
     *   THCON_SEC0: TileDescriptor@64, REG2@72, REG3@76, REG7@92, UNP0_BASE1@49, UNP0_XY1@56, UNP0_ZW1@57
     *   THCON_SEC1: TileDescriptor@112, REG2@120, REG3@124, REG7@140, UNP1_BASE1@61, UNP1_XY1@58, UNP1_ZW1@59
     */
    /* StateID from ThreadConfig[CurrentThread].CFG_STATE_ID_StateID (ADDR32=0, bit 0).
     * Selects which Config bank (Config[0] vs Config[1]) to read from.
     * Config[1] registers are at offset +224 (CFG_STATE_SIZE*4) from Config[0].
     */
    uint32_t state_id = tt->thd_reg[tid][0] & 0x1;
    uint32_t cfg_bank_offset = state_id * 224;

    /* MultiContextMode: determine which config context to use.
     * ISA: WhichContext = ContextCounter + CfgContextOffset[WhichUnpacker]
     * CfgContextOffset is stored in ThreadConfig register 41 (UNPACK_MISC_CFG):
     *   CfgContextOffset_0: bits[3:0]  (for unpacker 0)
     *   CfgContextOffset_1: bits[11:8] (for unpacker 1)
     * When WhichContext != 0, use REG3_Base_cntx[N] and REG7_Offset_cntx[N]
     * instead of REG3_Base_address and REG7_Offset_address.
     */
    uint32_t which_ctx = 0;
    if (multi_ctx_mode) {
        uint32_t misc_cfg = tt->thd_reg[tid][41];
        uint32_t ctx_offset = (which_unp == 0) ? (misc_cfg & 0xF) : ((misc_cfg >> 8) & 0xF);
        /* TODO: add ContextCounter support when needed */
        which_ctx = ctx_offset & 0x3; /* max 4 contexts */
    }

    /* Config register offsets based on which unpacker */
    uint32_t td_base     = (which_unp == 0) ? 64  : 112; /* TileDescriptor */
    uint32_t reg2_off    = (which_unp == 0) ? 72  : 120; /* Out_data_format */
    uint32_t reg3_off    = (which_unp == 0) ? 76  : 124; /* Base_address (context 0) */
    uint32_t reg7_off    = (which_unp == 0) ? 92  : 140; /* Offset_address (context 0) */
    uint32_t out_base_off = (which_unp == 0) ? 49 : 61;  /* ADDR_BASE_REG_1 */
    uint32_t out_xy_off  = (which_unp == 0) ? 56  : 58;  /* ADDR_CTRL_XY_REG_1 */
    uint32_t out_zw_off  = (which_unp == 0) ? 57  : 59;  /* ADDR_CTRL_ZW_REG_1 */

    /* In MultiContextMode with context != 0, use context-specific registers.
     * SEC0: Base_cntx[1..3] = reg 77..79, Offset_cntx[1..3] = reg 93..95
     * SEC1: Base_cntx[1..3] = reg 125..127, Offset_cntx[1..3] = reg 141..143
     */
    if (which_ctx != 0) {
        reg3_off += which_ctx;  /* 76+1=77, 76+2=78, 76+3=79 (or 124+N for SEC1) */
        reg7_off += which_ctx;  /* 92+1=93, 92+2=94, 92+3=95 (or 140+N for SEC1) */
    }

    /* Apply Config bank offset: Config[0] at base, Config[1] at +224 */
    td_base += cfg_bank_offset;
    reg2_off += cfg_bank_offset;
    reg3_off += cfg_bank_offset;
    reg7_off += cfg_bank_offset;
    out_base_off += cfg_bank_offset;
    out_xy_off += cfg_bank_offset;
    out_zw_off += cfg_bank_offset;

    /* Read TileDescriptor (128-bit = 4 x 32-bit words) from high_mem */
    uint32_t td0 = tensix_read_cfg(&tt->mem, td_base + 0);
    uint32_t td1 = tensix_read_cfg(&tt->mem, td_base + 1);
    uint32_t td2 = tensix_read_cfg(&tt->mem, td_base + 2);
    uint32_t td3 = tensix_read_cfg(&tt->mem, td_base + 3);

    uint32_t in_data_fmt    = td0 & 0xF;           /* InDataFormat [3:0] */
    /* uint32_t is_uncompressed = (td0 >> 4) & 0x1; */
    uint32_t x_dim          = (td0 >> 16) & 0xFFFF; /* XDim [31:16] */
    uint32_t y_dim          = td1 & 0xFF;            /* YDim [39:32] */
    uint32_t z_dim          = (td1 >> 16) & 0xFF;    /* ZDim [55:48] */
    uint32_t w_dim          = td2 & 0xFF;            /* WDim [71:64] */
    uint32_t digest_size    = (td3 >> 24) & 0xFF;    /* DigestSize [127:120] */

    if (z_dim == 0) z_dim = 1;
    if (w_dim == 0) w_dim = 1;

    /* Out data format for output address conversion */
    uint32_t out_data_fmt = tensix_read_cfg(&tt->mem, reg2_off) & 0xF;

    /* L1 base and offset addresses from high_mem */
    uint32_t base_addr   = tensix_read_cfg(&tt->mem, reg3_off);
    uint32_t offset_addr = tensix_read_cfg(&tt->mem, reg7_off) & 0xFFFF;

    /* L1 input byte address: (base + offset + 1 + digest_size) * 16 */
    uint32_t in_addr = (base_addr + offset_addr + 1 + digest_size) * 16;

    TT_DBG("[UNPACR] cfg: base_addr(cfg[%d])=0x%x, offset(cfg[%d])=0x%x, digest=%d, in_addr=0x%x\n",
           reg3_off, base_addr, reg7_off, offset_addr, digest_size, in_addr);

    /* Datum size from input format */
    uint32_t dsb = get_datum_size(in_data_fmt);

    /* ADC counters */
    AddrCtrl *adc = &tt->adc[which_unp];
    uint32_t x_pos = adc->ch0_x;
    uint32_t y_pos = adc->ch0_y;
    uint32_t x_end = adc->ch1_x + 1;

    /* Datum count from ADC x-counters */
    uint32_t input_num_datums = (x_end > x_pos) ? (x_end - x_pos) : 0;
    if (input_num_datums > 1024) input_num_datums = 1024;

    /* First datum index (uncompressed path).
     * Use input_num_datums as the face stride: firmware may set x_dim=0
     * in the TileDescriptor (e.g. SrcA), relying on ADC ch0_z to step
     * through faces.  Each face contains input_num_datums datums.
     */
    uint32_t face_stride = (x_dim > 0) ? x_dim : input_num_datums;
    uint32_t first_datum = ((adc->ch0_w * z_dim + adc->ch0_z) * y_dim + y_pos) * face_stride + x_pos;

    /* Input data byte address in L1 */
    uint32_t in_addr_datums = in_addr + first_datum * dsb;

    /* Output address: ADDR_BASE_REG_1 + ADC Channel[1] * strides - read from high_mem */
    uint32_t out_base    = tensix_read_cfg(&tt->mem, out_base_off) & 0x3FFFF;
    uint32_t out_ystride = (tensix_read_cfg(&tt->mem, out_xy_off) >> 16) & 0xFFFF;
    uint32_t out_zstride = tensix_read_cfg(&tt->mem, out_zw_off) & 0xFFFF;
    uint32_t out_wstride = (tensix_read_cfg(&tt->mem, out_zw_off) >> 16) & 0xFFFF;

    uint32_t out_addr = out_base
        + adc->ch1_y * out_ystride
        + adc->ch1_z * out_zstride
        + adc->ch1_w * out_wstride;

    /* Convert byte address to datum index based on OutDataFormat */
    switch (out_data_fmt) {
    case 0: case 4: case 8: /* FP32, TF32, Int32: 4 bytes/datum */
        out_addr >>= 2;
        break;
    case 1: case 5: case 9: case 12: /* FP16, BF16, Int16, UInt16: 2 bytes/datum */
        out_addr >>= 1;
        break;
    /* 1-byte formats: no shift needed */
    }

    /* Main unpack loop: read from L1, convert, write to SrcA/SrcB */
    TT_DBG("[UNPACR] which=%d, in_addr=0x%x, num_datums=%d, out_addr=%d, in_fmt=%d, out_fmt=%d\n",
           which_unp, in_addr_datums, input_num_datums, out_addr, in_data_fmt, out_data_fmt);
    if (tt->mem.l1_scratchpad != NULL && input_num_datums > 0) {
        for (uint32_t i = 0; i < input_num_datums; i++) {
            float val = 0.0f;

            if (!zero_write) {
                uint32_t l1_byte = in_addr_datums + i * dsb;
                if (l1_byte + dsb <= 0x180000) {
                    uint32_t raw = 0;
                    memcpy(&raw, tt->mem.l1_scratchpad + l1_byte, dsb);
                    val = datum_to_float(raw, in_data_fmt);
                    if (i < 4) TT_DBG("[UNPACR]   L1[0x%x] raw=0x%x -> val=%f\n", l1_byte, raw, val);
                }
            }

            /* Compute output row/col from datum index */
            uint32_t datum_idx = out_addr + i;
            uint32_t row = datum_idx / 16;
            uint32_t col = datum_idx & 15;

            if (which_unp == 1) {
                /* Write to SrcB */
                row = row & 0x3F;
                if (row < SRCB_ROWS)
                    tt->srcb[row][col] = val;
            } else {
                /* Write to SrcA */
                row = row & 0x3F;
                if (row < SRCA_ROWS)
                    tt->srca[row][col] = val;
            }
        }
        TT_DBG("[UNPACR] After: Src%c[0][0..3] = %f %f %f %f\n", which_unp ? 'B' : 'A',
               which_unp ? tt->srcb[0][0] : tt->srca[0][0],
               which_unp ? tt->srcb[0][1] : tt->srca[0][1],
               which_unp ? tt->srcb[0][2] : tt->srca[0][2],
               which_unp ? tt->srcb[0][3] : tt->srca[0][3]);
    }

    /* Set data valid (give src bank to matrix unit) */
    if (set_dat_valid) {
        if (which_unp == 0)
            tt->srca_dvalid = true;
        else
            tt->srcb_dvalid = true;
    }

    /* Update ADC counters based on AddrMode.
     * AddrMode[7:0]: Ch1YInc[7:6] | Ch1ZInc[5:4] | Ch0YInc[3:2] | Ch0ZInc[1:0]
     */
    uint32_t ch0_z_inc = addr_mode_field & 0x3;
    uint32_t ch0_y_inc = (addr_mode_field >> 2) & 0x3;
    uint32_t ch1_z_inc = (addr_mode_field >> 4) & 0x3;
    uint32_t ch1_y_inc = (addr_mode_field >> 6) & 0x3;

    adc->ch0_y += ch0_y_inc;
    adc->ch0_z += ch0_z_inc;
    adc->ch1_y += ch1_y_inc;
    adc->ch1_z += ch1_z_inc;
    return true;
}
static bool ttunpacr_nop(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttrstdma(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetdmareg(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETDMAREG(Payload_SigSelSize, Payload_SigSel, SetSignalsMode, RegIndex16b)
     * Encoding: Payload_SigSelSize<<22 | Payload_SigSel<<8 | SetSignalsMode<<7 | RegIndex16b<<0
     *
     * Two modes:
     * 1. Immediate mode (SetSignalsMode=0):
     *    Write 16-bit immediate to GPR half-register.
     *    In immediate mode, bits 23:8 form the 16-bit value (SigSelSize bits
     *    are part of the value, not a separate field).
     *    uint16_t *HalfReg = (uint16_t*)&GPRs[CurrentThread][0] + ResultHalfReg;
     *    *HalfReg = NewValue;
     *
     * 2. Special mode (SetSignalsMode=1):
     *    Read packer configuration/state into GPRs.
     *    ResultSize = bits 23:22
     *    Within Payload_SigSel (bits 21:8):
     *      InputHalfReg  = bits 10:8  (3 bits)
     *      InputSource   = bits 14:11 (4 bits)
     *      WhichPackers  = bits 18:15 (4 bits)
     *    ResultHalfReg   = bits 6:0   (7 bits)
     */
    uint32_t set_signals = (imm >> 7) & 0x1;
    uint32_t reg_index = imm & 0x7F;

    if (set_signals == 0) {
        /* Immediate mode: write 16-bit value to GPR half.
         * bits 23:8 = 16-bit immediate value */
        uint16_t value = (uint16_t)((imm >> 8) & 0xFFFF);
        uint16_t *half_regs = (uint16_t *)&tt->dma_reg[tid][0];
        if (reg_index < (DMA_REG_COUNT * 2)) {  /* 64 regs * 2 halves = 128 */
            half_regs[reg_index] = value;
        }
    } else {
        /* Special mode: read packer configuration/state into GPRs.
         *
         * For the emulator, we don't model detailed packer hardware state
         * (tile sizes, AllZeroFlags, exponent histograms, etc.).
         * We return zeros for all InputSource values, but handle the
         * ResultSize field correctly to write the proper number of GPRs.
         */
        uint32_t result_size = (imm >> 22) & 0x3;
        /* uint32_t input_half_reg = (imm >> 8) & 0x7; */
        /* uint32_t input_source   = (imm >> 11) & 0xF; */
        /* uint32_t which_packers  = (imm >> 15) & 0xF; */

        uint32_t result_half = reg_index;
        uint32_t *gprs = tt->dma_reg[tid];

        /* Values from packer state - all zeros in emulator */
        uint32_t values[4] = {0, 0, 0, 0};

        switch (result_size) {
        case 0: /* 16-bit: write one half-reg */
        {
            uint16_t *half_regs = (uint16_t *)gprs;
            if (result_half < (DMA_REG_COUNT * 2)) {
                uint16_t *src_halves = (uint16_t *)values;
                uint32_t input_half_reg = (imm >> 8) & 0x7;
                half_regs[result_half] = src_halves[input_half_reg];
            }
            break;
        }
        case 1: /* 32-bit: write one full GPR */
        {
            uint32_t gpr_index = result_half >> 1;
            uint32_t src_index = ((imm >> 8) & 0x7) >> 1;
            if (gpr_index < DMA_REG_COUNT) {
                gprs[gpr_index] = values[src_index];
            }
            break;
        }
        case 2: /* 128-bit: write four consecutive GPRs */
        {
            uint32_t base = (result_half >> 1) & 0x3C;
            for (int i = 0; i < 4 && (base + i) < DMA_REG_COUNT; i++) {
                gprs[base + i] = values[i];
            }
            break;
        }
        case 3: /* 128-bit, tile header mode (preserve reserved fields) */
        {
            uint32_t base = (result_half >> 1) & 0x3C;
            /* Write non-reserved fields from values (all zeros in emulator).
             * TileHeader layout:
             *   word 0: TileSize(16), reserved(16)
             *   word 1: reserved(16), DataFormat(4), DisableZeroCompress(1), SpareBits(3), reserved(8)
             *   word 2: AllZeroFlags(32)
             *   word 3: reserved(32)
             */
            if (base + 3 < DMA_REG_COUNT) {
                /* word 0: write TileSize (lower 16 bits), preserve upper 16 */
                gprs[base + 0] = (gprs[base + 0] & 0xFFFF0000) | (values[0] & 0x0000FFFF);
                /* word 1: write DataFormat+flags (bits 23:16), preserve rest */
                gprs[base + 1] = (gprs[base + 1] & 0xFF00FFFF) | (values[1] & 0x00FF0000);
                /* word 2: write AllZeroFlags (full 32 bits) */
                gprs[base + 2] = values[2];
                /* word 3: reserved, leave unchanged */
            }
            break;
        }
        }
    }
    return true;
}
static bool ttflushdma(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttreg2flop(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttloadind(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttpacr_setreg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool tttbufcmd(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetadc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetadcxy(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETADCXY(CntSetMask, Ch1_Y, Ch1_X, Ch0_Y, Ch0_X, BitMask)
     * Encoding: CntSetMask<<21 | Ch1_Y<<15 | Ch1_X<<12 | Ch0_Y<<9 | Ch0_X<<6 | BitMask<<0
     *
     * ISA functional model (SETADCXY.md):
     *   CntSetMask[2:0]: bit0=U0(Unpacker0), bit1=U1(Unpacker1), bit2=PK(Packer)
     *   Ch1_Y[5:0]: bits[5:3]=ThreadOverride(u2), bits[2:0]=Y1Val(u3)
     *   BitMask[3:0]: bit0=X0, bit1=Y0, bit2=X1, bit3=Y1
     *
     *   WhichThread = ThreadOverride==0 ? CurrentThread : ThreadOverride-1;
     *   ApplyTo selected ADC units:
     *     if (X0) ADC.Channel[0].X = X0Val, X_Cr = X0Val;
     *     if (Y0) ADC.Channel[0].Y = Y0Val, Y_Cr = Y0Val;
     *     if (X1) ADC.Channel[1].X = X1Val, X_Cr = X1Val;
     *     if (Y1) ADC.Channel[1].Y = Y1Val, Y_Cr = Y1Val;
     */
    uint32_t cnt_set_mask = (imm >> 21) & 0x7;
    uint32_t y1_val = (imm >> 15) & 0x7;
    uint32_t x1_val = (imm >> 12) & 0x7;
    uint32_t y0_val = (imm >> 9) & 0x7;
    uint32_t x0_val = (imm >> 6) & 0x7;
    uint32_t bitmask = imm & 0xF;

    /* adc[0]=Unpacker0, adc[1]=Unpacker1, adc[2]=Packer */
    for (int t = 0; t < 3; t++) {
        if (!((cnt_set_mask >> t) & 1)) continue;
        AddrCtrl *a = &tt->adc[t];
        if (bitmask & 0x1) { a->ch0_x = x0_val; a->ch0_x_cr = x0_val; }
        if (bitmask & 0x2) { a->ch0_y = y0_val; a->ch0_y_cr = y0_val; }
        if (bitmask & 0x4) { a->ch1_x = x1_val; a->ch1_x_cr = x1_val; }
        if (bitmask & 0x8) { a->ch1_y = y1_val; a->ch1_y_cr = y1_val; }
    }
    return true;
}
static bool ttincadcxy(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttaddrcrxy(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetadczw(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETADCZW(CntSetMask, Ch1_W, Ch1_Z, Ch0_W, Ch0_Z, BitMask)
     * Encoding: CntSetMask<<21 | Ch1_W<<15 | Ch1_Z<<12 | Ch0_W<<9 | Ch0_Z<<6 | BitMask<<0
     *
     * ISA functional model (SETADCZW.md):
     *   Same structure as SETADCXY but for Z and W counters.
     *   BitMask[3:0]: bit0=Z0, bit1=W0, bit2=Z1, bit3=W1
     */
    uint32_t cnt_set_mask = (imm >> 21) & 0x7;
    uint32_t w1_val = (imm >> 15) & 0x7;
    uint32_t z1_val = (imm >> 12) & 0x7;
    uint32_t w0_val = (imm >> 9) & 0x7;
    uint32_t z0_val = (imm >> 6) & 0x7;
    uint32_t bitmask = imm & 0xF;

    for (int t = 0; t < 3; t++) {
        if (!((cnt_set_mask >> t) & 1)) continue;
        AddrCtrl *a = &tt->adc[t];
        if (bitmask & 0x1) { a->ch0_z = z0_val; a->ch0_z_cr = z0_val; }
        if (bitmask & 0x2) { a->ch0_w = w0_val; a->ch0_w_cr = w0_val; }
        if (bitmask & 0x4) { a->ch1_z = z1_val; a->ch1_z_cr = z1_val; }
        if (bitmask & 0x8) { a->ch1_w = w1_val; a->ch1_w_cr = w1_val; }
    }
    return true;
}
static bool ttincadczw(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttaddrcrzw(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetdvalid(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETDVALID(setvalid)
     * Encoding: setvalid<<0 (24-bit field, only bits[1:0] meaningful)
     *
     * ISA: bit0=FlipSrcA - give unpacker's SrcA bank to MatrixUnit
     *      bit1=FlipSrcB - give unpacker's SrcB bank to MatrixUnit
     */
    uint32_t setvalid = imm & 0x3;
    if (setvalid & 0x1)  /* FlipSrcA */
        tt->srca_dvalid = true;
    if (setvalid & 0x2)  /* FlipSrcB */
        tt->srcb_dvalid = true;
    return true;
}
static bool ttadddmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsubdmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttmuldmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttbitwopdmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttshiftdmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttcmpdmareg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttsetadcxx(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETADCXX(CntSetMask, x_end2, x_start)
     * Encoding: CntSetMask<<21 | x_end2<<10 | x_start<<0
     *
     * ISA functional model (SETADCXX.md):
     *   Unconditionally sets both channels' X values (no per-channel bitmask).
     *   Always uses CurrentThread (no ThreadOverride).
     *   X0Val = x_start (u10), X1Val = x_end2 (u10)
     *
     *   ApplyTo(ADC):
     *     ADC.Channel[0].X = X0Val, X_Cr = X0Val;
     *     ADC.Channel[1].X = X1Val, X_Cr = X1Val;
     */
    uint32_t cnt_set_mask = (imm >> 21) & 0x7;
    uint32_t x1_val = (imm >> 10) & 0x3FF;  /* x_end2: 10-bit */
    uint32_t x0_val = imm & 0x3FF;           /* x_start: 10-bit */

    for (int t = 0; t < 3; t++) {
        if (!((cnt_set_mask >> t) & 1)) continue;
        AddrCtrl *a = &tt->adc[t];
        a->ch0_x = x0_val; a->ch0_x_cr = x0_val;
        a->ch1_x = x1_val; a->ch1_x_cr = x1_val;
    }
    return true;
}
static bool ttdmanop(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttatincget(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttatincgetptr(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttatswap(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttatcas(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttstoreind(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttstorereg(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_STOREREG(TdmaDataRegIndex, RegAddr)
     * Encoding: TdmaDataRegIndex<<18 | RegAddr<<0
     *
     * ISA functional model (STOREREG.md):
     *   uint32_t Addr = 0xFFB00000 + (AddrLo << 2);
     *   if (Addr < 0xFFB11000) UndefinedBehaviour();
     *   *(uint32_t*)Addr = GPRs[CurrentThread][DataReg];
     *
     * Example: 0x6761600a
     *   DataReg = (0x61600a >> 18) & 0x3F = 24
     *   AddrLo = 0x61600a & 0x3FFFF = 0x1600a
     *   Addr = 0xFFB00000 + (0x1600a << 2) = 0xFFB58028
     *   Action: Write DMA_REG[24] to stream register 0xFFB58028
     */
    uint32_t data_reg = (imm >> 18) & 0x3F;
    uint32_t addr_lo = imm & 0x3FFFF;
    uint32_t target_addr = 0xFFB00000 + (addr_lo << 2);

    /* Check address range (must be >= 0xFFB11000) */
    if (target_addr < 0xFFB11000) {
        /* Undefined behavior - ignore the write */
        return true;
    }

    /* Read value from DMA register */
    uint32_t value = tt->dma_reg[tid][data_reg];

    /* Calculate offset in high_mem (high_mem maps 0xFFB00000 - 0xFFEFFFFF) */
    uint32_t offset = target_addr - 0xFFB00000;

    /* Write to high memory region */
    if (tt->mem.high_mem != NULL && offset < 0x400000) {  /* 4MB range check */
        *(uint32_t *)(tt->mem.high_mem + offset) = value;

        /* Stream overlay UPDATE register: write to UPDATE (reg 270, offset 0x438)
         * should extract increment and add to AVAILABLE (reg 297, offset 0x4A4).
         * UPDATE format: bits[22:6] = increment (17-bit signed), bits[5:0] = dest_num
         * Stream overlay range: 0xFFB40000 - 0xFFB7FFFF, each stream 0x1000 apart.
         */
        if (target_addr >= 0xFFB40000 && target_addr < 0xFFB80000) {
            uint32_t off_in_stream = (target_addr - 0xFFB40000) & 0xFFF;
            if (off_in_stream == 0x438) {  /* UPDATE reg offset */
                uint32_t raw = (value >> 6) & 0x1FFFF;
                int32_t increment = (raw & 0x10000) ? (int32_t)(raw | 0xFFFE0000) : (int32_t)raw;
                uint32_t avail_offset = offset + 0x6C;  /* 0x4A4 - 0x438 = 0x6C */
                if (avail_offset < 0x400000)
                    *(uint32_t *)(tt->mem.high_mem + avail_offset) += (uint32_t)increment;
            }
        }
    }
    return true;
}
static bool ttloadreg(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_LOADREG(TdmaDataRegIndex, RegAddr)
     * Encoding: TdmaDataRegIndex<<18 | RegAddr<<0
     *
     * ISA functional model (LOADREG.md):
     *   uint32_t* GPR = &GPRs[CurrentThread][ResultReg];
     *   uint32_t Addr = 0xFFB00000 + (AddrLo << 2);
     *   if (Addr < 0xFFB11000) UndefinedBehaviour();
     *   *GPR = *(uint32_t*)Addr;  // completes asynchronously
     */
    uint32_t result_reg = (imm >> 18) & 0x3F;
    uint32_t addr_lo = imm & 0x3FFFF;
    uint32_t target_addr = 0xFFB00000 + (addr_lo << 2);

    /* Check address range (must be >= 0xFFB11000) */
    if (target_addr < 0xFFB11000) {
        return true;
    }

    /* Calculate offset in high_mem (high_mem maps 0xFFB00000 - 0xFFEFFFFF) */
    uint32_t offset = target_addr - 0xFFB00000;

    /* Read from high memory region into DMA register */
    uint32_t value = 0;
    if (tt->mem.high_mem != NULL && offset < 0x400000) {  /* 4MB range check */
        value = *(uint32_t *)(tt->mem.high_mem + offset);
    }

    if (result_reg < DMA_REG_COUNT) {
        tt->dma_reg[tid][result_reg] = value;
    }
    return true;
}
static bool sfpload(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SFPLOAD(lreg_ind, instr_mod0, sfpu_addr_mode, dest_reg_addr)
     * Encoding: lreg_ind<<20 | instr_mod0<<16 | sfpu_addr_mode<<13 | dest_reg_addr<<0
     * Bits:     [23:20](4)    [19:16](4)       [15:13](3)           [12:0](13)
     */
    uint32_t lreg_ind       = (imm >> 20) & 0xF;
    uint32_t instr_mod0     = (imm >> 16) & 0xF;
    uint32_t sfpu_addr_mode = (imm >> 13) & 0x7;
    uint32_t dest_reg_addr  = imm & 0x1FFF;

    if (lreg_ind >= 8) {
        apply_addr_mod(tt, sfpu_addr_mode, tid);
        return true;  /* VD >= 8 disables write */
    }

    /* ISA (SFPLOAD.md) address calculation:
     *   Addr = Imm + ThreadConfig.DEST_TARGET_REG_CFG_MATH_Offset
     *        + RWCs.Dst + ConfigState.DEST_REGW_BASE_Base
     *   Row = (Addr & ~3) + (Lane / 8)
     *   Column = (Lane & 7) * 2
     *   if (Addr & 2) Column += 1         — bit[1] selects odd columns
     */
    uint32_t math_offset = tt->thd_reg[tid][1];
    uint32_t addr = dest_reg_addr + math_offset + tt->dest_rwc[tid];
    uint32_t odd_col = (addr & 2) ? 1 : 0;
    for (int lane = 0; lane < LREG_LANES; lane++) {
        uint32_t row = (addr & ~3) + (lane / 8);
        uint32_t col = (lane & 7) * 2 + odd_col;
        if (row < DEST_ROWS && col < ROW_SIZE) {
            float val = tt->dest[row][col];
            memcpy(&tt->lreg[lreg_ind][lane], &val, 4);
        }
    }

    apply_addr_mod(tt, sfpu_addr_mode, tid);
    return true;
}
static bool sfploadi(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpstore(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SFPSTORE(dest_reg_addr, lreg_ind, instr_mod0, sfpu_addr_mode)
     * Encoding: lreg_ind<<20 | instr_mod0<<16 | sfpu_addr_mode<<13 | dest_reg_addr<<0
     * Bits:     [23:20](4)    [19:16](4)       [15:13](3)           [12:0](13)
     */
    uint32_t lreg_ind       = (imm >> 20) & 0xF;
    uint32_t instr_mod0     = (imm >> 16) & 0xF;
    uint32_t sfpu_addr_mode = (imm >> 13) & 0x7;
    uint32_t dest_reg_addr  = imm & 0x1FFF;

    (void)instr_mod0; /* TODO: data format conversion */

    /* ISA (SFPSTORE.md) address calculation — same as SFPLOAD:
     *   Addr = Imm + ThreadConfig.DEST_TARGET_REG_CFG_MATH_Offset
     *        + RWCs.Dst + ConfigState.DEST_REGW_BASE_Base
     */
    uint32_t math_offset = tt->thd_reg[tid][1];
    uint32_t addr = dest_reg_addr + math_offset + tt->dest_rwc[tid];
    uint32_t odd_col = (addr & 2) ? 1 : 0;
    for (int lane = 0; lane < LREG_LANES; lane++) {
        uint32_t row = (addr & ~3) + (lane / 8);
        uint32_t col = (lane & 7) * 2 + odd_col;
        if (row < DEST_ROWS && col < ROW_SIZE) {
            float val;
            memcpy(&val, &tt->lreg[lreg_ind][lane], 4);
            tt->dest[row][col] = val;
        }
    }

    apply_addr_mod(tt, sfpu_addr_mode, tid);
    return true;
}
static bool sfplut(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpmuli(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpaddi(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpdivp2(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpexexp(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpexman(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpiadd(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpshft(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpsetcc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpmov(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpabs(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpand(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpor(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpnot(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfplz(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpsetexp(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpsetman(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpmad(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpadd(tensix_t *tt, uint32_t imm, int tid) {
    uint32_t src_a = (imm >> 16) & 0xF;
    uint32_t src_b = (imm >> 12) & 0xF;
    uint32_t src_c = (imm >> 8)  & 0xF;
    uint32_t dest  = (imm >> 4)  & 0xF;

    if (dest >= 8) return true;  /* VD >= 8 disables write */

    /* MAD: dest = src_a * src_b + src_c */
    for (int lane = 0; lane < LREG_LANES; lane++) {
        float a, b, c;
        memcpy(&a, &tt->lreg[src_a][lane], 4);
        memcpy(&b, &tt->lreg[src_b][lane], 4);
        memcpy(&c, &tt->lreg[src_c][lane], 4);
        float result = a * b + c;
        memcpy(&tt->lreg[dest][lane], &result, 4);
    }

    return true;
}
static bool sfpmul(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfppushc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfppopc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpsetsgn(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpencc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpcompc(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfptransp(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpxor(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfp_stoch_rnd(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpnop(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpcast(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpconfig(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpswap(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfploadmacro(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpshft2(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfplutfp32(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfple(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpgt(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfpmul24(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool sfparecip(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttatgetm(tensix_t *tt, uint32_t imm, int tid) {
    uint32_t mutex_id = imm & 0x7;
    if (tt->mutex[mutex_id] == MUTEX_NONE) {
        tt->mutex[mutex_id] = tid;
    }
    return true;
}
static bool ttatrelm(tensix_t *tt, uint32_t imm, int tid) {
    uint32_t mutex_id = imm & 0x7;
    if (tt->mutex[mutex_id] == tid) {
        tt->mutex[mutex_id] = MUTEX_NONE;
    }
    return true;
}
static bool ttstallwait(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttseminit(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SEMINIT(max_value, init_value, sem_sel)
     * Encoding: max_value<<20 | init_value<<16 | sem_sel<<2
     *
     * ISA: sem_sel is an 8-bit bitmask selecting which semaphores to init.
     *      For each selected semaphore: Value = init_value, Max = max_value.
     */
    uint32_t max_val   = (imm >> 20) & 0xF;
    uint32_t init_val  = (imm >> 16) & 0xF;
    uint32_t sem_mask  = (imm >> 2) & 0xFF;

    for (int i = 0; i < 8; i++) {
        if (sem_mask & (1 << i)) {
            tt->sem[i] = init_val;
            tt->sem_max[i] = max_val;
        }
    }

    return true;
}
static bool ttsempost(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SEMPOST(sem_sel)
     * Encoding: sem_sel<<2
     *
     * ISA: sem_sel is an 8-bit bitmask. For each selected semaphore,
     *      increment Value if Value < 15.
     */
    uint32_t sem_mask = (imm >> 2) & 0xFF;

    for (int i = 0; i < 8; i++) {
        if ((sem_mask & (1 << i)) && tt->sem[i] < 15) {
            tt->sem[i]++;
        }
    }
    TT_DBG("[SEMPOST] core=%d mask=0x%x sem0=%d sem1=%d dest_dvalid=%d\n",
           tid, sem_mask, tt->sem[0], tt->sem[1], tt->dest_dvalid);

    /* When MATH core (thread_id==1) posts a semaphore, all ELWADDs in the
     * current MOP have completed and Dest is fully populated.  Signal this
     * to the PACR wait-gate so packing can proceed.
     */
    if (tid == 1) {
        tt->dest_dvalid = true;
    }
    return true;
}
static bool ttsemget(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SEMGET(sem_sel)
     * Encoding: sem_sel<<2
     *
     * ISA: sem_sel is an 8-bit bitmask. For each selected semaphore,
     *      decrement Value if Value > 0.
     */
    uint32_t sem_mask = (imm >> 2) & 0xFF;

    for (int i = 0; i < 8; i++) {
        if ((sem_mask & (1 << i)) && tt->sem[i] > 0) {
            tt->sem[i]--;
        }
    }
    TT_DBG("[SEMGET] core=%d mask=0x%x sem0=%d sem1=%d\n",
           tid, sem_mask, tt->sem[0], tt->sem[1]);
    return true;
}
static bool ttsemwait(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SEMWAIT(stall_res, sem_sel, wait_sem_cond)
     * Encoding: stall_res<<15 | sem_sel<<2 | wait_sem_cond<<0
     *
     * Note: SEMWAIT is normally intercepted by the Wait Gate in tensix_cop.c
     * and never reaches this function. This is a fallback stub.
     */
    (void)tt;
    (void)imm;
    return true;
}
static bool ttstreamwait(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttwrcfg(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_WRCFG(GprAddress, wr128b, CfgReg)
     * Encoding: GprAddress<<16 | wr128b<<15 | CfgReg<<0
     *
     * ISA functional model (WRCFG.md):
     *   StateID = ThreadConfig[CurrentThread].CFG_STATE_ID_StateID;
     *   if (Is128Bit)
     *     memcpy(&Config[StateID][CfgIndex & ~3], &GPRs[CurrentThread][InputReg & ~3], 16);
     *   else
     *     Config[StateID][CfgIndex] = GPRs[CurrentThread][InputReg];
     */
    uint32_t gpr_addr  = (imm >> 16) & 0x3F;
    uint32_t wr128b    = (imm >> 15) & 0x1;
    uint32_t cfg_index = imm & 0x7FFF;

    uint32_t state_id = tt->thd_reg[tid][0] & 0x1;

    if (wr128b) {
        /* 128-bit write: copy 4 consecutive 32-bit registers to high_mem */
        uint32_t cfg_base = cfg_index & ~0x3U;
        uint32_t gpr_base = gpr_addr & ~0x3U;
        for (int i = 0; i < 4; i++) {
            if ((cfg_base + i) < CFG_REG_COUNT && (gpr_base + i) < DMA_REG_COUNT) {
                uint32_t value = tt->dma_reg[tid][gpr_base + i];
                tensix_write_cfg(&tt->mem, cfg_base + i, value);
                TT_DBG("[WRCFG] thread=%d, cfg[%d] = 0x%x (from dma_reg[%d])\n",
                       tid, cfg_base + i, value, gpr_base + i);
            }
        }
    } else {
        /* 32-bit write to high_mem */
        if (cfg_index < CFG_REG_COUNT && gpr_addr < DMA_REG_COUNT) {
            uint32_t value = tt->dma_reg[tid][gpr_addr];
            tensix_write_cfg(&tt->mem, cfg_index, value);
            TT_DBG("[WRCFG] thread=%d, cfg[%d] = 0x%x (from dma_reg[%d])\n",
                   tid, cfg_index, value, gpr_addr);
        }
    }
    return true;
}
static bool ttrdcfg(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_RDCFG(GprAddress, CfgReg)
     * Encoding: GprAddress<<16 | CfgReg<<0
     *
     * ISA functional model (RDCFG.md):
     *   StateID = ThreadConfig[CurrentThread].CFG_STATE_ID_StateID;
     *   GPRs[CurrentThread][GprAddress] = Config[StateID][CfgReg];
     */
    uint32_t gpr_addr  = (imm >> 16) & 0x3F;
    uint32_t cfg_index = imm & 0xFFFF;

    /* Read from high_mem where config registers are stored */
    if (cfg_index < CFG_REG_COUNT && gpr_addr < DMA_REG_COUNT) {
        tt->dma_reg[tid][gpr_addr] = tensix_read_cfg(&tt->mem, cfg_index);
    }
    return true;
}
static bool ttsetc16(tensix_t *tt, uint32_t imm, int tid) {
    /* ckernel_ops.h: TT_OP_SETC16(setc16_reg, setc16_value)
     * Encoding: setc16_reg<<16 | setc16_value<<0
     *
     * ISA functional model (SETC16.md):
     *   ThreadConfig[CurrentThread][CfgIndex].Value = NewValue;
     *
     * This writes to per-thread configuration (ThreadConfig), not shared Config.
     * ThreadConfig stores addr_mod settings, cfg_state_id, etc.
     */
    uint32_t cfg_index = (imm >> 16) & 0xFF;
    uint32_t value     = imm & 0xFFFF;

    if (cfg_index < THD_REG_COUNT) {
        tt->thd_reg[tid][cfg_index] = value;
    }
    return true;
}
/* RMWCIB helper: Read-Modify-Write Configuration Interface Block
 * Encoding: Mask<<16 | NewValue<<8 | Index4<<0
 * Index1 is 0/1/2/3 from instruction variant (RMWCIB0/1/2/3)
 *
 * ISA functional model:
 *   StateID = ThreadConfig[CurrentThread].CFG_STATE_ID_StateID;
 *   CfgAddress = (uint8_t*)&Config[StateID][Index4] + Index1;
 *   OldValue = *CfgAddress;
 *   *CfgAddress = (NewValue & Mask) | (OldValue & ~Mask);
 */
static void rmwcib_common(tensix_t *tt, uint32_t imm, uint32_t byte_offset)
{
    uint32_t mask      = (imm >> 16) & 0xFF;
    uint32_t new_value = (imm >> 8) & 0xFF;
    uint32_t index4    = imm & 0xFF;

    /* Bounds check: Index4 must be within cfg_reg array */
    if (index4 >= CFG_REG_COUNT || !tt->mem.high_mem)
        return;

    /* Access cfg in high_mem as bytes */
    uint8_t *cfg_bytes = tt->mem.high_mem + TENSIX_CFG_OFFSET_IN_HIGH_MEM + index4 * 4;
    uint8_t old_byte = cfg_bytes[byte_offset];
    cfg_bytes[byte_offset] = (new_value & mask) | (old_byte & ~mask);
}
static bool ttrmwcib0(tensix_t *tt, uint32_t imm, int tid) { rmwcib_common(tt, imm, 0); return true; }
static bool ttrmwcib1(tensix_t *tt, uint32_t imm, int tid) { rmwcib_common(tt, imm, 1); return true; }
static bool ttrmwcib2(tensix_t *tt, uint32_t imm, int tid) { rmwcib_common(tt, imm, 2); return true; }
static bool ttrmwcib3(tensix_t *tt, uint32_t imm, int tid) { rmwcib_common(tt, imm, 3); return true; }
static bool ttstreamwrcfg(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }
static bool ttcfgshiftmask(tensix_t *tt, uint32_t imm, int tid) { (void)tt; (void)imm; (void)tid; return true; }

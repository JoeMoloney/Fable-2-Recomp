// Readable rewrite of sub_83122F18 (guest 0x83122F18, 22 PPC instructions, 35 callers).
// Gate: python3 tools/theseus/gate.py sub_83122F18
//
// Updates the 5-bit field in the low bits of the word at r3 from the flag bits
// in r4 (bits 1, 2): starting from the current field f, clears bit 2 of it when
// both had a "2"-type flag, then merges r4's bits in; the upper 27 bits of the
// word are kept:
//   if ((f & 4) && (r4 & 2)) f -= 4;
//   v = r4; if (f & v & 4) { f -= 4; v -= 4; }
//   if ((f & 1) && (v & 2)) v -= 2;
//   word = (word & ~0x1F) | ((f | v) & 0x1F);
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_83122F18) {
  const uint32_t addr = ctx.r3.u32;
  const uint32_t word = REX_LOAD_U32(addr);
  uint32_t f = word & 0x1F, v = ctx.r4.u32;
  if ((f & 4) && (v & 2)) f -= 4;
  if (f & v & 4) {
    f -= 4;
    v -= 4;
  }
  if ((f & 1) && (v & 2)) v -= 2;
  REX_STORE_U32(addr, (word & ~0x1Fu) | ((f | v) & 0x1F));
}

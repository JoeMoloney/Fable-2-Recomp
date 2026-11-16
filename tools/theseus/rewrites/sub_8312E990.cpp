// Readable rewrite of sub_8312E990 (guest 0x8312E990, 13 PPC instructions, 17 callers).
// Gate: python3 tools/theseus/gate.py sub_8312E990
//
// r3 = descriptor. Returns the low 16 bits of the word at +16; when any of bits
// 16..18 of that word are set, adds the index of the lowest set bit of the
// 4-bit field at bits 1..4 of the word at +8 (31 - clz(lowest bit); -1 when the
// field is 0). 64-bit arithmetic, so the result can wrap below zero.
// Name: unknown (bit-field arithmetic on a packed descriptor).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_8312E990) {
  const uint32_t desc = ctx.r3.u32;
  const uint32_t w = REX_LOAD_U32(desc + 16);
  uint64_t r = w & 0xFFFF;
  if (w & 0x70000) {
    const uint32_t field = (REX_LOAD_U32(desc + 8) >> 1) & 0xF;
    const uint32_t lowest = field & (0u - field);
    const uint64_t clz = lowest ? __builtin_clz(lowest) : 32;
    r = r - clz + 31;
  }
  ctx.r3.u64 = r;
}

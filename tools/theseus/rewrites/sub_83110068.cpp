// Readable rewrite of sub_83110068 (guest 0x83110068, 21 PPC instructions, 27 callers).
// Gate: python3 tools/theseus/gate.py sub_83110068
//
// r3 = object; r3+12 points at a typed node whose word +8 holds a 7-bit code
// (bits 7..13) and a flag (bit 5). Returns 1 when any of bits 25..27 of the
// object's first word are set, the code is not 124/125 and the flag is clear;
// else 0.
// Name: unknown (same code field as sub_8310F980).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_83110068) {
  const uint32_t obj = ctx.r3.u32;
  bool yes = false;
  if (REX_LOAD_U32(obj + 0) & 0xE000000) {
    const uint32_t w = REX_LOAD_U32(REX_LOAD_U32(obj + 12) + 8);
    const uint32_t code = (w >> 7) & 0x7F;
    yes = code != 124 && code != 125 && !(w & 0x20);
  }
  ctx.r3.u64 = yes ? 1 : 0;
}

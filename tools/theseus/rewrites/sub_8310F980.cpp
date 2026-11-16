// Readable rewrite of sub_8310F980 (guest 0x8310F980, 19 PPC instructions, 17 callers).
// Gate: python3 tools/theseus/gate.py sub_8310F980
//
// r3 = object; w = word at +8. Returns 1 when flag bit 0x40 is set and the 7-bit
// code in bits 7..13 is in 21..24 or 59..66, else 0. Same code field as
// sub_8311C250 / sub_8310FA10 (a type/kind classifier).
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_8310F980) {
  const uint32_t w = REX_LOAD_U32(ctx.r3.u32 + 8);
  const uint32_t code = (w >> 7) & 0x7F;
  const bool in_range = (code >= 21 && code <= 24) || (code >= 59 && code <= 66);
  ctx.r3.u64 = ((w & 0x40) && in_range) ? 1 : 0;
}

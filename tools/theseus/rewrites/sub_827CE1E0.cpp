// Readable rewrite of sub_827CE1E0 (guest 0x827CE1E0, 22 PPC instructions, 22 callers).
// Gate: python3 tools/theseus/gate.py sub_827CE1E0
//
// Bounds-checked lookup in a std::vector of 8-byte elements (+8 first, +12 last):
// returns the first word of element r4, or 0 when the vector is empty or r4 >=
// size (unsigned; size = (last - first) / 8 as a 32-bit arithmetic shift). The
// original then repeats the check as a debug assert (trap); it can never fire
// after the check above, so it is left out.
// Proposed name: std_vector8_GetFirstWord_827CE1E0 (evidence.py: vector helper).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_827CE1E0) {
  const uint32_t vec = ctx.r3.u32, index = ctx.r4.u32;
  const uint32_t first = REX_LOAD_U32(vec + 8);
  if (first == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  const uint32_t size = uint32_t(int32_t(REX_LOAD_U32(vec + 12) - first) >> 3);
  ctx.r3.u64 = index < size ? REX_LOAD_U32(first + index * 8) : 0;
}

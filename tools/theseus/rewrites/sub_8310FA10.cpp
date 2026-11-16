// Readable rewrite of sub_8310FA10 (guest 0x8310FA10, 22 PPC instructions, 28 callers).
// Gate: python3 tools/theseus/gate.py sub_8310FA10
//
// r3 = object; code = bits 7..13 of the word at +8 (same field as sub_8310F980).
// Returns 1 for codes 21..24, 63..66, 108 and 109, else 0.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_8310FA10) {
  const uint32_t code = (REX_LOAD_U32(ctx.r3.u32 + 8) >> 7) & 0x7F;
  const bool yes = (code >= 21 && code <= 24) || (code >= 63 && code <= 66) ||
                   code == 108 || code == 109;
  ctx.r3.u64 = yes ? 1 : 0;
}

// Readable rewrite of sub_8311C250 (guest 0x8311C250, 21 PPC instructions, 15 callers).
// Gate: python3 tools/theseus/gate.py sub_8311C250
//
// r3 = object; code = bits 7..13 of the word at +8 (same field as sub_8310F980).
// Returns 1 for codes 1..15 and 25..31 except 29, else 0.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_8311C250) {
  const uint32_t code = (REX_LOAD_U32(ctx.r3.u32 + 8) >> 7) & 0x7F;
  bool yes = false;
  if (code >= 1 && code <= 31) yes = code < 16 || (code > 24 && code != 29);
  ctx.r3.u64 = yes ? 1 : 0;
}

// Readable rewrite of sub_83145CA0 (guest 0x83145CA0, 22 PPC instructions, 32 callers).
// Gate: python3 tools/theseus/gate.py sub_83145CA0
//
// A 2-bit state kept in the low bits of a tagged pointer (r3), updated by the
// flag bits 0..2 of r4 as below. Returns (low 32 bits of r3 & ~3) | new state.
//   s = r3 & 3; if (r4 & 1) s = (s & 1) | 2;
//   if ((r4 & 2) && s != 3) s = 2;
//   if ((r4 & 4) && s != 3) s = (s & 1) ? 2 : (s >> 1) & 1;
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_83145CA0) {
  const uint32_t flags = ctx.r4.u32;
  uint32_t s = ctx.r3.u32 & 3;
  if (flags & 1) s = (s & 1) | 2;
  if ((flags & 2) && s != 3) s = 2;  // gate: equivalent & 3: bit 0 already made s 2 or 3
  if ((flags & 4) && s != 3) s = (s & 1) ? 2 : (s >> 1) & 1;
  ctx.r3.u64 = (ctx.r3.u32 & ~3u) | s;
}

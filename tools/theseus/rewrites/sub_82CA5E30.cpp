// Readable rewrite of sub_82CA5E30 (guest 0x82CA5E30, 15 PPC instructions, 17 callers).
// Gate: python3 tools/theseus/gate.py sub_82CA5E30
//
// CRT memchr(s = r3, c = r4, n = r5): the first of the n (low 32 bits) bytes
// equal to c (as int, against the zero-extended byte), else 0. The original
// only writes the low 32 bits of the pointer register, so a returned pointer
// keeps the high 32 bits of r3.
// Proposed name: memchr_82CA5E30.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_82CA5E30) {
  const uint64_t high = ctx.r3.u64 & ~uint64_t(0xFFFFFFFF);
  const uint32_t s = ctx.r3.u32, n = ctx.r5.u32;
  const int32_t c = ctx.r4.s32;
  for (uint32_t i = 0; i < n; ++i) {
    if (REX_LOAD_U8(s + i) == c) {
      ctx.r3.u64 = high | (s + i);
      return;
    }
  }
  ctx.r3.u64 = 0;
}

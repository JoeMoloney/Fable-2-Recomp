// Readable rewrite of SearchStringForChar_82CA70C0 (guest 0x82CA70C0, 22 PPC
// instructions, 12 callers). Gate: python3 tools/theseus/gate.py SearchStringForChar_82CA70C0
//
// CRT strrchr(s = r3, c = r4): the last position of c (as int, compared with the
// zero-extended byte) in s, or 0; c == 0 finds the terminator. The original only
// writes the low 32 bits of the pointer register, so a returned pointer keeps
// the high 32 bits of r3.
// Proposed name: strrchr_82CA70C0.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_SearchStringForChar_82CA70C0) {
  const uint64_t high = ctx.r3.u64 & ~uint64_t(0xFFFFFFFF);
  const int32_t c = ctx.r4.s32;
  uint32_t p = ctx.r3.u32;
  if (c == 0) {
    while (REX_LOAD_U8(p) != 0) ++p;
    ctx.r3.u64 = high | p;
    return;
  }
  uint64_t last = 0;
  for (;; ++p) {
    const int32_t b = REX_LOAD_U8(p);
    if (b == c)
      last = high | p;
    else if (b == 0)
      break;
  }
  ctx.r3.u64 = last;
}

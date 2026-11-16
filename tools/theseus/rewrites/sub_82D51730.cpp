// Readable rewrite of sub_82D51730 (guest 0x82D51730, 11 PPC instructions,
// 45 callers). Gate: python3 tools/theseus/gate.py sub_82D51730
//
// strcmp: compares unsigned bytes of r3 and r4 until they differ or r3's string
// ends; returns the difference of the last pair (a - b as a 64-bit value, so
// negative differences are sign-extended).
// Proposed name: StrCmp_82D51730.
#include "fable_2_pch.h"

namespace {

int64_t StrCmp(uint8_t* base, uint32_t a, uint32_t b) {
  for (uint32_t i = 0;; ++i) {
    const int64_t ca = REX_LOAD_U8(a + i);
    const int64_t diff = ca - int64_t(REX_LOAD_U8(b + i));
    if (ca == 0 || diff != 0) return diff;
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D51730) {
  ctx.r3.s64 = StrCmp(base, ctx.r3.u32, ctx.r4.u32);
}

// Readable rewrite of sub_82D51980 (guest 0x82D51980, 9 PPC instructions,
// 49 callers). Gate: python3 tools/theseus/gate.py sub_82D51980
//
// strlen: r3 = string; returns the length as a 32-bit value (zero-extended:
// the original ends with rotlwi r3,r11,0).
// Proposed name: StrLen_82D51980.
#include "fable_2_pch.h"

namespace {

uint32_t StrLen(uint8_t* base, uint32_t str) {
  uint32_t n = 0;
  while (REX_LOAD_U8(str + n) != 0) ++n;
  return n;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D51980) {
  ctx.r3.u64 = StrLen(base, ctx.r3.u32);
}

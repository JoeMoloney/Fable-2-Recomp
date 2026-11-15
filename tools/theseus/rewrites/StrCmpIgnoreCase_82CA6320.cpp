// Readable rewrite of StrCmpIgnoreCase_82CA6320 (guest 0x82CA6320, 21 PPC
// instructions, 211 callers). Test: python3 tools/theseus/difftest.py StrCmpIgnoreCase_82CA6320
//
// C runtime _stricmp: compares two strings ignoring ASCII case (only 'A'-'Z'
// are folded). r3 = string a, r4 = string b. Returns a's char minus b's char
// at the first difference (after folding), as a full 64-bit signed value;
// 0 if equal. Stops when b ends (returning a's char there).
#include "fable_2_pch.h"

namespace {

uint32_t ToLowerAscii(uint32_t c) { return (c >= 'A' && c <= 'Z') ? (c | 0x20) : c; }

int64_t StrCmpIgnoreCase(uint8_t* base, uint32_t a, uint32_t b) {
  for (uint32_t i = 0;; ++i) {
    const uint32_t cb = REX_LOAD_U8(b + i);
    const uint32_t ca = REX_LOAD_U8(a + i);
    const int64_t diff = int64_t(ca) - int64_t(cb);
    if (cb == 0) return diff;
    if (diff == 0) continue;
    const int64_t folded = int64_t(ToLowerAscii(ca)) - int64_t(ToLowerAscii(cb));
    if (folded != 0) return folded;
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_StrCmpIgnoreCase_82CA6320) {
  ctx.r3.s64 = StrCmpIgnoreCase(base, ctx.r3.u32, ctx.r4.u32);
}

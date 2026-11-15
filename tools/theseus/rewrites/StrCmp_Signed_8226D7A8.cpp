// Readable rewrite of StrCmp_Signed_8226D7A8 (guest 0x8226D7A8, 21 PPC
// instructions, 169 callers). Test: python3 tools/theseus/difftest.py StrCmp_Signed_8226D7A8
//
// strcmp comparing bytes as SIGNED chars (so bytes >= 0x80 sort before 'A').
// r3 = string a, r4 = string b. Returns -1, 0 or 1 (full 64-bit register).
#include "fable_2_pch.h"

namespace {

int64_t StrCmpSigned(uint8_t* base, uint32_t a, uint32_t b) {
  for (uint32_t i = 0;; ++i) {
    const int8_t ca = int8_t(REX_LOAD_U8(a + i));
    const int8_t cb = int8_t(REX_LOAD_U8(b + i));
    if (ca == 0 && cb == 0) return 0;
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_StrCmp_Signed_8226D7A8) {
  ctx.r3.s64 = StrCmpSigned(base, ctx.r3.u32, ctx.r4.u32);
}

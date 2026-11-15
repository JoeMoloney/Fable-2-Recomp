// Readable rewrite of StrLenUTF16_8217AB30 (guest 0x8217AB30, 9 PPC instructions,
// 44 callers). Test: python3 tools/theseus/difftest.py StrLenUTF16_8217AB30
//
// wcslen for 16-bit characters: r3 = string, returns the number of 16-bit
// units before the zero terminator. The original computes it as
// (byte distance to just past the terminator) / 2 - 1 using a 32-bit signed
// shift; the same formula is kept so huge lengths behave identically.
#include "fable_2_pch.h"

namespace {

int64_t StrLenUtf16(uint8_t* base, uint32_t str) {
  uint64_t units = 0;
  while (REX_LOAD_U16(str + uint32_t(units * 2)) != 0) ++units;
  const uint32_t bytes_past_end = uint32_t((units + 1) * 2);
  return int64_t(int32_t(bytes_past_end) >> 1) - 1;
}

}  // namespace

extern "C" REX_FUNC(rewrite_StrLenUTF16_8217AB30) {
  ctx.r3.s64 = StrLenUtf16(base, ctx.r3.u32);
}

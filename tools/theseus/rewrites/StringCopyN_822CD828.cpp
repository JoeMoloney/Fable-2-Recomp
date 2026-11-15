// Readable rewrite of StringCopyN_822CD828 (guest 0x822CD828, 23 PPC instructions,
// 56 callers). Differential test: python3 tools/theseus/difftest.py StringCopyN_822CD828
// (input rules in StringCopyN_822CD828.rules).
//
// This is the C library's strncpy:
//   r3 = destination, r4 = source, r5 = count (low 32 bits used)
//   copies bytes up to and including the source's terminating zero, but at most
//   `count` bytes; if the string was shorter, fills the rest of the `count`
//   bytes with zeros. Returns the destination (r3 is left unchanged).
//
// Bytes are copied one at a time, front to back, exactly like the original, so
// overlapping source/destination behave the same way.
// Scratch registers (r4, r5, r9-r11, ctr, cr0, cr6, xer) are not reproduced:
// tools/theseus/caller_usage.py shows whether any caller reads them.
#include "fable_2_pch.h"

namespace {

void StringCopyN(uint8_t* base, uint32_t dst, uint32_t src, uint32_t count) {
  uint32_t i = 0;
  for (; i < count; ++i) {
    const uint8_t c = REX_LOAD_U8(src + i);
    REX_STORE_U8(dst + i, c);
    if (c == 0) {
      ++i;
      break;
    }
  }
  for (; i < count; ++i) REX_STORE_U8(dst + i, 0);
}

}  // namespace

extern "C" REX_FUNC(rewrite_StringCopyN_822CD828) {
  StringCopyN(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

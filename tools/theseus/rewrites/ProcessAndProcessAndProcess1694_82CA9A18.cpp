// Readable rewrite of ProcessAndProcessAndProcess1694_82CA9A18 (guest 0x82CA9A18,
// 17 PPC instructions, 9 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1694_82CA9A18
//
// CRT wcscmp(a = r3, b = r4) on 16-bit characters: -1, 0 or 1 by the first
// differing character (unsigned), 0 when both end together.
// Proposed name: wcscmp_82CA9A18.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1694_82CA9A18) {
  uint32_t a = ctx.r3.u32, b = ctx.r4.u32;
  int32_t diff;
  for (;; a += 2, b += 2) {
    const uint32_t cb = REX_LOAD_U16(b);
    diff = int32_t(REX_LOAD_U16(a) - cb);
    if (diff != 0 || cb == 0) break;
  }
  ctx.r3.s64 = diff < 0 ? -1 : diff > 0 ? 1 : 0;
}

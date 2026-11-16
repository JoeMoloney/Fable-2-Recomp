// Readable rewrite of StringCompare_82CA3920 (guest 0x82CA3920, 19 PPC
// instructions, 41 callers). Gate: python3 tools/theseus/gate.py StringCompare_82CA3920
//
// strncmp (MSVC CRT): compares at most n unsigned bytes of r3 and r4, stopping
// after a difference or at the end of r3's string; returns the difference of the
// last pair compared (64-bit, sign-extended), or 0 when n <= 0 (n = r5 as a
// signed 32-bit count).
// Proposed name: StrNCmp_82CA3920.
#include "fable_2_pch.h"

namespace {

int64_t StrNCmp(uint8_t* base, uint32_t a, uint32_t b, int32_t n) {
  int64_t diff = 0;
  for (int32_t i = 0; i < n; ++i) {
    const int64_t ca = REX_LOAD_U8(a + i);
    diff = ca - int64_t(REX_LOAD_U8(b + i));
    if (ca == 0 || diff != 0) break;
  }
  return diff;
}

}  // namespace

extern "C" REX_FUNC(rewrite_StringCompare_82CA3920) {
  ctx.r3.s64 = StrNCmp(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.s32);
}

// Readable rewrite of sub_82CA6B90 (guest 0x82CA6B90, 7 PPC instructions, 7 callers).
// Gate: python3 tools/theseus/gate.py sub_82CA6B90
//
// MSVC CRT isalnum(c): the locale ctype table (_pctype, pointer at 0x832EF350)
// entry for c, masked with _ALPHA|_UPPER|_LOWER|_DIGIT (0x107).
// Proposed name: isalnum_82CA6B90.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kPctype = 0x832EF350;
constexpr uint32_t kAlnumMask = 0x107;  // _UPPER | _LOWER | _DIGIT | _ALPHA
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82CA6B90) {
  const uint32_t table = REX_LOAD_U32(kPctype);
  ctx.r3.u64 = REX_LOAD_U16(table + ctx.r3.u32 * 2) & kAlnumMask;
}

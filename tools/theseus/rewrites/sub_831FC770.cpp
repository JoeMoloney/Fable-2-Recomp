// Readable rewrite of sub_831FC770 (guest 0x831FC770, 9 PPC instructions, 12 callers).
// Gate: python3 tools/theseus/gate.py sub_831FC770
//
// Circular intrusive list with the sentinel at r3+12 and next links at +8:
// counts the nodes from the first (r3's word +8) until the sentinel comes back.
// Proposed name: List_Count_831FC770.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kFirst = 8, kSentinel = 12, kNext = 8;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_831FC770) {
  const uint32_t sentinel = ctx.r3.u32 + kSentinel;
  uint64_t count = 0;
  for (uint32_t n = REX_LOAD_U32(ctx.r3.u32 + kFirst); n != sentinel; n = REX_LOAD_U32(n + kNext))
    ++count;
  ctx.r3.u64 = count;
}

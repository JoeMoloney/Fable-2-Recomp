// Readable rewrite of sub_82D005A0 (guest 0x82D005A0, 21 PPC instructions, 9 callers).
// Gate: python3 tools/theseus/gate.py sub_82D005A0
//
// r3 = container with `count` (+8) entry pointers at +1128; each entry starts
// with a 64-bit id. Returns the first entry whose id equals r4 (all 64 bits),
// or 0.
// Name: unknown (find by 64-bit id, e.g. a XUID or content id).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kCount = 8, kEntries = 1128;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D005A0) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t count = REX_LOAD_U32(obj + kCount);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entry = REX_LOAD_U32(obj + kEntries + i * 4);
    if (REX_LOAD_U64(entry) == ctx.r4.u64) {
      ctx.r3.u64 = entry;
      return;
    }
  }
  ctx.r3.u64 = 0;
}

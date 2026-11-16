// Readable rewrite of sub_82D4F340 (guest 0x82D4F340, 10 PPC instructions, 22 callers).
// Gate: python3 tools/theseus/gate.py sub_82D4F340
//
// Chunked array (see sub_82D4F368): returns the sum (64-bit) of the element
// counts (+28) of r3 and every chunk chained after it through +4.
// Proposed name: ChunkList_TotalCount_82D4F340.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kNext = 4, kCount = 28;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D4F340) {
  const uint32_t first = ctx.r3.u32;
  uint64_t total = REX_LOAD_U32(first + kCount);
  for (uint32_t c = REX_LOAD_U32(first + kNext); c != 0; c = REX_LOAD_U32(c + kNext))
    total += REX_LOAD_U32(c + kCount);
  ctx.r3.u64 = total;
}

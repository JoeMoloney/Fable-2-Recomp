// Readable rewrite of sub_82D4F368 (guest 0x82D4F368, 26 PPC instructions, 14 callers).
// Gate: python3 tools/theseus/gate.py sub_82D4F368
//
// Chunked array: a chain of chunks linked through +4, each with a base pointer
// (+24) and an element count (+28); elements are 24 bytes. r3 = first chunk,
// r4 = position counted back from the total (k = r4 - total). Walks the chunks
// adding each count to k; at the first chunk where k (as int32) is >= 0, returns
// base + 24 * k (64-bit sum). If none, returns the first chunk's base.
// Name: unknown (sibling of sub_82D4F340, which sums the counts).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kNext = 4, kBase = 24, kCount = 28, kElemSize = 24;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D4F368) {
  const uint32_t first = ctx.r3.u32;
  uint64_t total = REX_LOAD_U32(first + kCount);
  for (uint32_t c = REX_LOAD_U32(first + kNext); c != 0; c = REX_LOAD_U32(c + kNext))
    total += REX_LOAD_U32(c + kCount);
  uint64_t k = ctx.r4.u64 - total;
  uint32_t chunk = first;
  for (;;) {
    k += REX_LOAD_U32(chunk + kCount);
    if (int32_t(k) >= 0) {
      ctx.r3.u64 = uint64_t(uint32_t(k) * kElemSize) + REX_LOAD_U32(chunk + kBase);
      return;
    }
    chunk = REX_LOAD_U32(chunk + kNext);
    if (chunk == 0) break;
  }
  ctx.r3.u64 = REX_LOAD_U32(first + kBase);
}

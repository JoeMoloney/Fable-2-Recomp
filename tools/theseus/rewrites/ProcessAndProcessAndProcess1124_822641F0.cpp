// Readable rewrite of ProcessAndProcessAndProcess1124_822641F0 (guest 0x822641F0,
// 53 PPC instructions, 145 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1124_822641F0
//
// Lookup in a sorted vector of 12-byte entries { int key; u32 value; ... }
// (r3 = vector: +8 first, +12 last), r4 = key: std::lower_bound by key, then
// returns the entry's value if its key equals r4, else 0. The original keeps
// its iterator in the red zone below r1 (dead after the return; not copied).
// Proposed name: SortedVector_FindValue_822641F0.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kFirst = 8, kLast = 12, kEntrySize = 12, kEntryKey = 0, kEntryValue = 4;
}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1124_822641F0) {
  const uint32_t vec = ctx.r3.u32;
  const int32_t key = ctx.r4.s32;
  uint32_t first = REX_LOAD_U32(vec + kFirst);
  const uint32_t last = REX_LOAD_U32(vec + kLast);
  int32_t count = int32_t(last - first) / int32_t(kEntrySize);
  while (count > 0) {  // lower_bound
    const int32_t half = count >> 1;
    const uint32_t mid = first + uint32_t(half) * kEntrySize;
    if (int32_t(REX_LOAD_U32(mid + kEntryKey)) < key) {
      first = mid + kEntrySize;
      count -= half + 1;
    } else {
      count = half;
    }
  }
  const bool found = first != last && !(key < int32_t(REX_LOAD_U32(first + kEntryKey)));
  const uint32_t it = found ? first : last;
  ctx.r3.u64 = it != REX_LOAD_U32(vec + kLast) ? REX_LOAD_U32(it + kEntryValue) : 0;
}

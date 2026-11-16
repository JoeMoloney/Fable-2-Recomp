// Readable rewrite of sub_82D5A318 (guest 0x82D5A318, 27 PPC instructions,
// 31 callers). Gate: python3 tools/theseus/gate.py sub_82D5A318
//
// Integer-keyed hash map get with a default:
//   r3 = map { +0 u32* slots; +8 u32 mask }, r4 = key, r5 = default (64-bit).
// slots holds mask+1 keys (0xFFFFFFFF = empty) followed by mask+1 values.
// Fibonacci hashing (Knuth): start at ((key >> 4) * 0x9E3779B1) & mask, probe
// linearly; returns the value of the matching key (32-bit) or the default.
// Proposed name: IntHashMap_Get_82D5A318.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kSlots = 0, kMask = 8;
constexpr uint32_t kEmptyKey = 0xFFFFFFFF;
constexpr uint32_t kGoldenRatio = 0x9E3779B1;

uint64_t Get(uint8_t* base, uint32_t map, uint32_t key, uint64_t fallback) {
  const uint32_t slots = REX_LOAD_U32(map + kSlots);
  const uint32_t mask = REX_LOAD_U32(map + kMask);
  for (uint32_t i = ((key >> 4) * kGoldenRatio) & mask;; i = (i + 1) & mask) {
    const uint32_t slot_key = REX_LOAD_U32(slots + i * 4);
    if (slot_key == kEmptyKey) return fallback;
    if (slot_key == key) return REX_LOAD_U32(slots + (mask + 1 + i) * 4);
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D5A318) {
  ctx.r3.u64 = Get(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u64);
}

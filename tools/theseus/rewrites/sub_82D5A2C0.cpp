// Readable rewrite of sub_82D5A2C0 (guest 0x82D5A2C0, 21 PPC instructions, 19 callers).
// Gate: python3 tools/theseus/gate.py sub_82D5A2C0
//
// Open-addressing hash set of u32 keys: r3 = set { +0 u32* slots; +8 u32 mask },
// r4 = key. Slot value 0xFFFFFFFF = empty. Hash = (key >> 4) * 0x9E3779B1
// (Fibonacci hashing), linear probing. Returns the slot index of the key, or
// mask + 1 (= capacity, "end") when an empty slot is reached first.
// Proposed name: HashSet_FindIndex_82D5A2C0.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kSlots = 0, kMask = 8;
constexpr uint32_t kEmpty = 0xFFFFFFFF;
constexpr uint32_t kGolden = 0x9E3779B1;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D5A2C0) {
  const uint32_t set = ctx.r3.u32, key = ctx.r4.u32;
  const uint32_t mask = REX_LOAD_U32(set + kMask);
  const uint32_t slots = REX_LOAD_U32(set + kSlots);
  uint32_t i = ((key >> 4) * kGolden) & mask;
  for (;;) {
    const uint32_t v = REX_LOAD_U32(slots + i * 4);
    if (v == kEmpty) {
      ctx.r3.u64 = uint64_t(mask) + 1;
      return;
    }
    if (v == key) break;
    i = (i + 1) & mask;
  }
  ctx.r3.u64 = i;
}

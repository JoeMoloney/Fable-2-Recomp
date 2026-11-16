// Readable rewrite of HashTable_Find2_829FC630 (guest 0x829FC630, 27 PPC
// instructions, 35 callers, e.g. HashTable_Insert_829FC6A0).
// Gate: python3 tools/theseus/gate.py HashTable_Find2_829FC630
//
// Open-addressing hash set lookup with linear probing:
//   r3 = table { +0 u32 capacity (power of two); +4 Entry** buckets },
//   r4 = key; Entry = { u32 key; value... at +4 }.
// Probes from key & (capacity - 1) until an empty bucket or a matching key;
// returns a pointer to the entry's value (entry + 4, 64-bit add) or 0.
// Proposed name: HashTable_FindValue_829FC630.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kCapacity = 0, kBuckets = 4;
constexpr uint32_t kEntryKey = 0, kEntryValue = 4;

uint64_t FindValue(uint8_t* base, uint32_t table, uint32_t key) {
  const uint32_t mask = REX_LOAD_U32(table + kCapacity) - 1;
  const uint32_t buckets = REX_LOAD_U32(table + kBuckets);
  uint32_t slot = key & mask;
  for (;;) {
    const uint32_t entry = REX_LOAD_U32(buckets + slot * 4);
    if (entry == 0 || REX_LOAD_U32(entry + kEntryKey) == key) break;
    slot = (slot + 1) & mask;
  }
  const uint32_t entry = REX_LOAD_U32(buckets + slot * 4);
  return entry ? uint64_t(entry) + kEntryValue : 0;
}

}  // namespace

extern "C" REX_FUNC(rewrite_HashTable_Find2_829FC630) {
  ctx.r3.u64 = FindValue(base, ctx.r3.u32, ctx.r4.u32);
}

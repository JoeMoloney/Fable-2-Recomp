// Readable rewrite of sub_821E3E10 (guest 0x821E3E10, 25 PPC instructions,
// 174 callers). Test: python3 tools/theseus/difftest.py sub_821E3E10
//
// uint32_t HashMapU32_Find(const Map* map /* r3 */, uint32_t key /* r4 */):
// open addressing with linear probing, capacity a power of two, entries
// {u32 key, u32 value}, key 0 = empty slot. Returns the value, or 0 when an
// empty slot is reached first. Evidence: e.g. 0x8217E6D0 looks up the global
// map *0x83496BD0 with an id from an FNV-1 hash (FNV1Hash_821F3C28) and
// treats 0 as "not found". Better name: HashMapU32_Find.
//   map: +0 table; table: +16 entries, +20 capacity
// The slot index is (capacity - 1) & key computed on the full 64-bit key
// register, then compared/wrapped as 32-bit, like the original.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kTableEntries = 16, kTableCapacity = 20;
constexpr uint32_t kEntrySize = 8, kEntryKey = 0, kEntryValue = 4;

uint32_t HashMapFind(uint8_t* base, uint32_t map, uint64_t key) {
  const uint32_t table = REX_LOAD_U32(map + 0);
  const uint32_t capacity = REX_LOAD_U32(table + kTableCapacity);
  const uint32_t entries = REX_LOAD_U32(table + kTableEntries);
  uint32_t slot = uint32_t((uint64_t(capacity) - 1) & key);
  for (;;) {
    const uint32_t slot_key = REX_LOAD_U32(entries + slot * kEntrySize + kEntryKey);
    if (slot_key == uint32_t(key)) return REX_LOAD_U32(entries + slot * kEntrySize + kEntryValue);
    if (slot_key == 0) return 0;
    if (++slot == capacity) slot = 0;
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_821E3E10) {
  ctx.r3.u64 = HashMapFind(base, ctx.r3.u32, ctx.r4.u64);
}

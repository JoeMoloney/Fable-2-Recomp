// Readable rewrite of HashLookupByKey_82228570 (guest 0x82228570, 26 PPC
// instructions, 9 callers). Gate: python3 tools/theseus/gate.py HashLookupByKey_82228570
//
// Same chained hash table as HashTable_Find3_821CF1D0, but the bucket count is a
// plain number (+8, a power of two) and buckets are at +20: r3 = table, r4 = key
// object (hash at +8). Nodes are 20 bytes { +8 key; +12 kind; +16 next }.
// Returns the first node of kind 4 whose key is r4 (the bucket head address is
// a 64-bit sum), else the static end node 0x82099760 (sign-extended).
// The original first walks the bucket's whole chain without using the result
// (left-over code); the walk is kept so a broken chain behaves the same.
// Name kept (HashLookupByKey).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kCount = 8, kBuckets = 20;
constexpr uint32_t kNodeSize = 20, kNodeKey = 8, kNodeKind = 12, kNodeNext = 16;
constexpr uint32_t kKeyHash = 8, kWantedKind = 4;
constexpr int64_t kEnd = int32_t(0x82099760);
}  // namespace

extern "C" REX_FUNC(rewrite_HashLookupByKey_82228570) {
  const uint32_t table = ctx.r3.u32, key = ctx.r4.u32;
  const uint64_t mask = uint64_t(REX_LOAD_U32(table + kCount)) - 1;
  const uint32_t bucket = uint32_t(mask & REX_LOAD_U32(key + kKeyHash));
  uint64_t node = uint64_t(uint32_t(bucket * kNodeSize)) + REX_LOAD_U32(table + kBuckets);
  uint32_t tail = uint32_t(node);
  do tail = REX_LOAD_U32(tail + kNodeNext);  // unused walk to the chain's end
  while (tail != 0);
  do {
    if (REX_LOAD_U32(uint32_t(node) + kNodeKind) == kWantedKind &&
        REX_LOAD_U32(uint32_t(node) + kNodeKey) == key) {
      ctx.r3.u64 = node;
      return;
    }
    node = REX_LOAD_U32(uint32_t(node) + kNodeNext);
  } while (node != 0);
  ctx.r3.s64 = kEnd;
}

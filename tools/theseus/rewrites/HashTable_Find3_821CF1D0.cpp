// Readable rewrite of HashTable_Find3_821CF1D0 (guest 0x821CF1D0, 23 PPC
// instructions, 13 callers). Gate: python3 tools/theseus/gate.py HashTable_Find3_821CF1D0
//
// Chained hash table keyed by object pointer: r3 = table { +7 u8 log2 bucket
// count; +16 Bucket* buckets }, r4 = key object (its hash is the word at +8).
// Buckets and chain nodes are 20 bytes: { +8 key; +12 kind; +16 next }. The
// bucket is hash & (2^log2 - 1) (log2 >= 32 gives mask ~0); the first node of
// kind 4 with key == r4 is returned (64-bit sum of buckets + offset for the
// bucket head), else the address 0x82090754 sign-extended (a static "end" node).
// Name kept (HashTable_Find3); proposed HashTable_FindByKeyPtr_821CF1D0.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kLog2 = 7, kBuckets = 16;
constexpr uint32_t kNodeSize = 20, kNodeKey = 8, kNodeKind = 12, kNodeNext = 16;
constexpr uint32_t kKeyHash = 8, kWantedKind = 4;
constexpr int64_t kEnd = int32_t(0x82090754);
}  // namespace

extern "C" REX_FUNC(rewrite_HashTable_Find3_821CF1D0) {
  const uint32_t table = ctx.r3.u32, key = ctx.r4.u32;
  const uint32_t log2 = REX_LOAD_U8(table + kLog2);
  const uint32_t count = (log2 & 0x20) ? 0 : 1u << (log2 & 0x1F);
  const uint64_t mask = uint64_t(count) - 1;
  const uint32_t bucket = uint32_t(mask & REX_LOAD_U32(key + kKeyHash));
  uint64_t node = uint64_t(uint32_t(bucket * kNodeSize)) + REX_LOAD_U32(table + kBuckets);
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

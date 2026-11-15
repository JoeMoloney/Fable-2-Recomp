// Readable rewrite of FNV1Hash_821F3C28 (guest 0x821F3C28, 22 PPC instructions,
// ~2,170 callers). Differential test: python3 tools/theseus/difftest.py FNV1Hash_821F3C28
//
// FNV-1 32-bit hash of a zero-terminated byte string:
//   r3 = guest address of the string
//   r4 = starting hash (the caller passes the FNV offset basis or a previous hash)
//   returns the hash in r3
// FNV-1 (not FNV-1a): multiply by the prime first, then xor in the byte.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kFnv32Prime = 0x01000193;  // 16777619

// PowerPC `mullw` multiplies the low 32 bits of two registers as signed values
// and keeps the full 64-bit product. Only the low 32 bits are the hash; the
// high half is kept so the result register matches the original exactly
// (callers copy the full register; see tools/theseus/caller_usage.py).
uint64_t Mullw(uint64_t a, uint32_t b) {
  return uint64_t(int64_t(int32_t(uint32_t(a))) * int64_t(int32_t(b)));
}

// Returns the full 64-bit result register. The hash is its low 32 bits.
// An empty string returns the starting value unchanged (all 64 bits).
uint64_t Fnv1Hash32(uint8_t* base, uint32_t string_addr, uint64_t hash) {
  for (uint32_t p = string_addr;; ++p) {
    const uint8_t c = REX_LOAD_U8(p);
    if (c == 0) break;
    hash = Mullw(hash, kFnv32Prime) ^ c;
  }
  return hash;
}

}  // namespace

extern "C" REX_FUNC(rewrite_FNV1Hash_821F3C28) {
  ctx.r3.u64 = Fnv1Hash32(base, ctx.r3.u32, ctx.r4.u64);
}

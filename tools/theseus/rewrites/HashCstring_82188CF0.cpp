// Readable rewrite of HashCstring_82188CF0 (guest 0x82188CF0, 19 PPC instructions,
// 282 callers). Differential test: python3 tools/theseus/difftest.py HashCstring_82188CF0
//
// "x65599" string hash: hash = hash * 65599 + c over the bytes as SIGNED chars,
// including the terminating zero (one last multiply), at most 100 bytes.
//   r3 = guest address of the string; returns the hash in r3 (full 64-bit
//   register: the multiply keeps the 64-bit product of the low 32 bits).
// An empty string returns 0.
#include "fable_2_pch.h"

namespace {

constexpr int32_t kMultiplier = 65599;  // 0x1003F
constexpr int kMaxBytes = 100;

uint64_t HashCstring(uint8_t* base, uint32_t str) {
  uint64_t hash = 0;
  if (REX_LOAD_U8(str) == 0) return hash;
  for (int i = 0; i < kMaxBytes; ++i) {
    const int64_t c = int8_t(REX_LOAD_U8(str + i));
    hash = uint64_t(int64_t(int32_t(uint32_t(hash))) * kMultiplier) + uint64_t(c);
    if (c == 0) break;
  }
  return hash;
}

}  // namespace

extern "C" REX_FUNC(rewrite_HashCstring_82188CF0) {
  ctx.r3.u64 = HashCstring(base, ctx.r3.u32);
}

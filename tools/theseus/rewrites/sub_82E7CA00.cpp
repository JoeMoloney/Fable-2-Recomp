// Readable rewrite of sub_82E7CA00 (guest 0x82E7CA00, 9 PPC instructions, 34 callers).
// Gate: python3 tools/theseus/gate.py sub_82E7CA00
//
// Hands out the next power-of-two bit from the global at 0x832FDA24: returns 0
// once the top bit is reached; otherwise doubles the global and returns the new
// value (e.g. allocating flag bits / ids).
// Name: unknown (next flag bit).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kNextBit = 0x832FDA24;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82E7CA00) {
  const uint32_t bit = REX_LOAD_U32(kNextBit);
  if (bit & 0x80000000) {
    ctx.r3.u64 = 0;
    return;
  }
  REX_STORE_U32(kNextBit, bit << 1);
  ctx.r3.u64 = uint32_t(bit << 1);
}

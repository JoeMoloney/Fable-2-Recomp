// Readable rewrite of sub_82F32EC0 (guest 0x82F32EC0, 19 PPC instructions, 9 callers).
// Gate: python3 tools/theseus/gate.py sub_82F32EC0
//
// r3 = object with an array of { int key; u32 value } pairs (+24 pointer,
// +28 count). Returns the value of the first pair whose key equals r4, or 0.
// Name: unknown (a small key/value map lookup).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kPairs = 24, kCount = 28, kPairSize = 8;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82F32EC0) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t count = REX_LOAD_U32(obj + kCount);
  const uint32_t pairs = REX_LOAD_U32(obj + kPairs);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t pair = pairs + i * kPairSize;
    if (int32_t(REX_LOAD_U32(pair)) == ctx.r4.s32) {
      ctx.r3.u64 = REX_LOAD_U32(pair + 4);
      return;
    }
  }
  ctx.r3.u64 = 0;
}

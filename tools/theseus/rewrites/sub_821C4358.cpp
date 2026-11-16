// Readable rewrite of sub_821C4358 (guest 0x821C4358, 13 PPC instructions,
// 26 callers). Gate: python3 tools/theseus/gate.py sub_821C4358
//
// Index of an object's element in a global pool of 20-byte elements:
//   element = *(r3 + 20); pool base = *(*(*(0x8333FD30) + 20) + 4).
// Returns (element - base) / 20 (32-bit, zero-extended), or -1 (all 64 bits)
// when the object has no element.
// Proposed name: none yet (GetPoolIndex-like; callers 0x821B..0x82E8 unnamed).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kPoolGlobal = 0x8333FD30;
constexpr uint32_t kObjectElement = 20, kManagerPool = 20, kPoolBase = 4;
constexpr uint32_t kElementSize = 20;

int64_t PoolIndex(uint8_t* base, uint32_t object) {
  const uint32_t element = REX_LOAD_U32(object + kObjectElement);
  if (element == 0) return -1;
  const uint32_t pool = REX_LOAD_U32(REX_LOAD_U32(kPoolGlobal) + kManagerPool);
  const uint32_t first = REX_LOAD_U32(pool + kPoolBase);
  return (element - first) / kElementSize;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_821C4358) {
  ctx.r3.s64 = PoolIndex(base, ctx.r3.u32);
}

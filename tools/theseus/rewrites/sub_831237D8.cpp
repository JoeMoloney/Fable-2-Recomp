// Readable rewrite of sub_831237D8 (guest 0x831237D8, 45 PPC instructions, 26 callers).
// Gate: python3 tools/theseus/gate.py sub_831237D8
//
// r3 = object with a list of descriptors (+4 first, +8 next). Every descriptor
// word holds up to 7 2-bit values (count = bits 25..27, values packed in bits
// 5..12; the original first tests "bit 30 or count != 0", which adds nothing
// since a zero count contributes nothing either way); the union of
// 1 << value over all of them is a 4-bit mask. Stores into the word at r3+8:
// bits 1..4 = mask, bits 14..16 = number of distinct values (popcount; the
// original reads it from the packed table 0x8DA691691448).
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kFirst = 4, kNext = 8, kState = 8;

uint32_t DescriptorMask(uint32_t w) {
  const uint32_t count = (w >> 25) & 7, values = (w >> 5) & 0xFF;
  uint32_t mask = 0;
  for (uint32_t i = 0; i < count; ++i) mask |= 1u << ((values >> (2 * i)) & 3);
  return mask;
}
}  // namespace

extern "C" REX_FUNC(rewrite_sub_831237D8) {
  const uint32_t obj = ctx.r3.u32;
  uint32_t mask = 0;
  for (uint32_t d = REX_LOAD_U32(obj + kFirst); d != 0; d = REX_LOAD_U32(d + kNext))
    mask |= DescriptorMask(REX_LOAD_U32(d));
  const uint32_t distinct = __builtin_popcount(mask);
  uint32_t state = REX_LOAD_U32(obj + kState);
  state = (state & ~0x1Eu) | (mask << 1);
  state = (state & ~0x1C000u) | (distinct << 14);
  REX_STORE_U32(obj + kState, state);
}

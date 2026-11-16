// Readable rewrite of sub_8311A550 (guest 0x8311A550, 40 PPC instructions, 22 callers).
// Gate: python3 tools/theseus/gate.py sub_8311A550
//
// Bitmap search: r3 = array of u64 words (bit k of the bitmap = bit k % 64 of
// word k / 64, LSB first), r4 = first bit to look at, r5 = number of words.
// Returns the index of the first set bit >= r4 within the words, or -1. The last
// word is always read (even when r4 is past it).
// Proposed name: BitArray_FindFirstSet_8311A550.
#include "fable_2_pch.h"

namespace {
uint64_t BitIndex(uint64_t words, uint64_t at, uint64_t bits) {
  const int32_t word_index = int32_t(uint32_t(at - words)) >> 3;
  const uint64_t lowest = bits & (0 - bits);
  return uint64_t(uint32_t(word_index) << 6) - __builtin_clzll(lowest) + 63;
}
}  // namespace

extern "C" REX_FUNC(rewrite_sub_8311A550) {
  const uint64_t words = ctx.r3.u64;
  const uint64_t last = uint64_t(ctx.r5.u32 << 3) + words - 8;
  uint64_t at = uint64_t((ctx.r4.u32 >> 6) << 3) + words;
  uint64_t mask = ~uint64_t(0) << (ctx.r4.u32 & 63);
  for (; uint32_t(at) < uint32_t(last); at += 8) {
    const uint64_t bits = REX_LOAD_U64(uint32_t(at)) & mask;
    if (bits) {
      ctx.r3.u64 = BitIndex(words, at, bits);
      return;
    }
    mask = ~uint64_t(0);
  }
  const uint64_t bits = REX_LOAD_U64(uint32_t(last)) & mask;
  ctx.r3.s64 = bits ? int64_t(BitIndex(words, last, bits)) : -1;
}

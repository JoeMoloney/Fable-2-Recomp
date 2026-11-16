// Readable rewrite of sub_8311C908 (guest 0x8311C908, 36 PPC instructions, 11 callers).
// Gate: python3 tools/theseus/gate.py sub_8311C908
//
// Extracts a bit field from a bit array of u32 words (LSB-first): r3 = array
// header; when bit 0 of its first word is set the bits are stored inline from
// r3 itself, otherwise that word points at a block whose bits start at +4.
// The field runs from bit r4 + 1 to bit r5 + 1 (inclusive) and may span two
// words; returns it shifted down to bit 0 (zero-extended).
// Proposed name: BitArray_GetBits_8311C908.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_8311C908) {
  const uint32_t first = ctx.r4.u32 + 1, last = ctx.r5.u32 + 1;
  const uint32_t header = REX_LOAD_U32(ctx.r3.u32 + 0);
  const uint64_t bits = (header & 1) ? ctx.r3.u64 : uint64_t(header) + 4;
  const uint32_t shift = first & 31;
  const uint64_t keep = uint64_t(uint32_t(2) << (last & 31)) - 1;  // bits 0..last%32
  const uint32_t first_word = uint32_t(bits + ((first >> 5) << 2));
  const uint32_t last_word = uint32_t(bits + ((last >> 5) << 2));
  const uint32_t low = REX_LOAD_U32(first_word) & (~0u << shift);
  if (first_word == last_word) {
    ctx.r3.u64 = uint32_t(low & keep) >> shift;
    return;
  }
  const uint32_t high = REX_LOAD_U32(last_word) & keep;
  // The high word's bits go above the low part; nothing when the field starts
  // on a word boundary (the PPC shift by 32 gives 0).
  const uint32_t high_part = shift ? high << (32 - shift) : 0;
  ctx.r3.u64 = high_part | (low >> shift);
}

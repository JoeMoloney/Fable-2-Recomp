// Readable rewrite of FindSubstring_821E6388 (guest 0x821E6388, 31 PPC
// instructions, 51 callers). Test: python3 tools/theseus/difftest.py FindSubstring_821E6388
//
// C runtime strstr: r3 = haystack, r4 = needle. Returns the address of the
// first place the needle occurs in the haystack, the haystack itself for an
// empty needle, or 0. (The original compares bytes as signed chars, but only
// for equality, so signedness does not matter.)
// The returned address is r3 + offset as a 64-bit add, like the original.
#include "fable_2_pch.h"

namespace {

constexpr int64_t kNotFound = -1;  // gate: equivalent: internal sentinel (mapped to 0 below), never a real offset

// Offset of the first match from the haystack start, or kNotFound.
int64_t FindSubstring(uint8_t* base, uint32_t haystack, uint32_t needle) {
  if (REX_LOAD_U8(needle) == 0) return 0;
  for (uint32_t k = 0; REX_LOAD_U8(haystack + k) != 0; ++k) {
    uint32_t i = 0;
    while (REX_LOAD_U8(needle + i) != 0 &&
           REX_LOAD_U8(haystack + k + i) == REX_LOAD_U8(needle + i))
      ++i;
    if (REX_LOAD_U8(needle + i) == 0) return k;
  }
  return kNotFound;
}

}  // namespace

extern "C" REX_FUNC(rewrite_FindSubstring_821E6388) {
  const int64_t offset = FindSubstring(base, ctx.r3.u32, ctx.r4.u32);
  ctx.r3.u64 = offset == kNotFound ? 0 : ctx.r3.u64 + uint64_t(offset);
}

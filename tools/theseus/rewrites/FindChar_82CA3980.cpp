// Readable rewrite of FindChar_82CA3980 (guest 0x82CA3980, 15 PPC instructions,
// 46 callers). Test: python3 tools/theseus/difftest.py FindChar_82CA3980
//
// C runtime strchr: r3 = string, r4 = character (compared as a full 32-bit
// int, so values above 255 never match). Returns the address of the first
// match, or 0 if the string ends first. Searching for 0 returns the address
// of the terminator.
// Detail kept from the original: a found address only replaces the LOW 32
// bits of r3 (the upper half keeps the caller's value); "not found" sets all
// 64 bits to 0.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kNotFound = 0xFFFFFFFF;  // gate: equivalent: internal sentinel (mapped to 0 below), never a valid string address

uint32_t FindChar(uint8_t* base, uint32_t str, int32_t ch) {
  for (uint32_t p = str;; ++p) {
    const uint32_t c = REX_LOAD_U8(p);
    if (ch == 0 && c == 0) return p;
    if (c == 0) return kNotFound;
    if (int32_t(c) == ch) return p;
  }
}

}  // namespace

extern "C" REX_FUNC(rewrite_FindChar_82CA3980) {
  const uint32_t found = FindChar(base, ctx.r3.u32, ctx.r4.s32);
  if (found == kNotFound) ctx.r3.u64 = 0;
  else ctx.r3.u32 = found;
}

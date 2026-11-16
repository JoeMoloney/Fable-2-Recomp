// Readable rewrite of LinearSearch_82284448 (guest 0x82284448, 26 PPC
// instructions, 13 callers). Gate: python3 tools/theseus/gate.py LinearSearch_82284448
//
// r3 = object with a small array of 8-byte entries: at +68 when bit 0 of the
// u16 at +28 is set, else at +32; the u8 at +31 is the entry count. Returns the
// address (64-bit: r3 + offset) of the first entry whose top 27 bits (word >> 5)
// equal r4, or 0.
// Name kept (LinearSearch).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kFlags = 28, kCount = 31;
constexpr uint32_t kEntriesInline = 32, kEntriesAlt = 68, kEntrySize = 8;
}  // namespace

extern "C" REX_FUNC(rewrite_LinearSearch_82284448) {
  const uint32_t obj = ctx.r3.u32;
  const uint64_t entries =
      ctx.r3.u64 + ((REX_LOAD_U16(obj + kFlags) & 1) ? kEntriesAlt : kEntriesInline);
  const uint32_t count = REX_LOAD_U8(obj + kCount);
  for (uint32_t i = 0; i < count; ++i) {
    if ((REX_LOAD_U32(uint32_t(entries) + i * kEntrySize) >> 5) == ctx.r4.u32) {
      ctx.r3.u64 = entries + i * kEntrySize;
      return;
    }
  }
  ctx.r3.u64 = 0;
}

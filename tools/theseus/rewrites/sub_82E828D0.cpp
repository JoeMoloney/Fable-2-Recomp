// Readable rewrite of sub_82E828D0 (guest 0x82E828D0, 29 PPC instructions, 9 callers).
// Gate: python3 tools/theseus/gate.py sub_82E828D0
//
// Reads element r4 of a packed array of w-bit values: r3 = { +4 u32 width w;
// +8 u8* bytes }. Bit numbering is LSB-first within bytes; the element's last
// bit is (r4 + 1) * w - 1, and bits are read from there downwards, so result
// bit i = array bit (last - i). Result bits 32 and up stay 0 (shift >= 32).
// Proposed name: PackedBits_GetReversed_82E828D0.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kWidth = 4, kBytes = 8;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82E828D0) {
  const uint32_t arr = ctx.r3.u32;
  const uint32_t width = REX_LOAD_U32(arr + kWidth);
  const uint32_t last = (ctx.r4.u32 + 1) * width - 1;
  uint32_t byte = REX_LOAD_U32(arr + kBytes) + (last >> 3);
  uint32_t bit = last & 7;
  uint64_t value = 0;
  uint32_t i = 0;
  do {
    const uint32_t shift = i & 0x3F;  // a 32-bit shift by 32..63 gives 0
    if ((REX_LOAD_U8(byte) & (1u << bit)) && shift < 32) value |= 1u << shift;
    if (bit == 0) {
      --byte;
      bit = 7;
    } else {
      --bit;
    }
    ++i;
  } while (i != width);
  ctx.r3.u64 = value;
}

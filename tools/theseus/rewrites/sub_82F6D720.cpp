// Readable rewrite of sub_82F6D720 (guest 0x82F6D720, 20 PPC instructions,
// 24 callers). Gate: python3 tools/theseus/gate.py sub_82F6D720
//
// Is the last 16-bit unit of a counted buffer zero? r3 = { +4 u16* data;
// +8 u32 count }. Returns 0 for an empty buffer, else (data[count-1] == 0).
// The original's inlined accessor yields a null pointer when data is null and
// still reads it (address 0: a fault), which this rewrite keeps.
// Proposed name: WideBuffer_EndsWithNul_82F6D720.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kData = 4, kCount = 8;

bool LastUnitIsZero(uint8_t* base, uint32_t buffer) {
  const uint32_t count = REX_LOAD_U32(buffer + kCount);
  if (count == 0) return false;
  const uint32_t data = REX_LOAD_U32(buffer + kData);
  const uint32_t last = data ? data + (count - 1) * 2 : 0;  // gate: equivalent: 0 or 1, the read faults in the null page either way
  return REX_LOAD_U16(last) == 0;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82F6D720) {
  ctx.r3.u64 = LastUnitIsZero(base, ctx.r3.u32) ? 1 : 0;
}

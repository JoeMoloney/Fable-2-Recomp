// Readable rewrite of IndirectCall_8310FC68 (guest 0x8310FC68, 32 PPC instructions,
// 124 callers). Gate: python3 tools/theseus/gate.py IndirectCall_8310FC68
//
// Not an indirect call: the bctr is a jump table. It computes a byte size:
//   size = (count + 11) * 4           count = r5 (32-bit)
//   plus a per-type extra for type codes 84..125 (r4), see kExtra below;
//   type 125 adds extra * 4 instead (extra = r6).
// Returns the size in r3 as a 64-bit value: the base is a 32-bit number and the
// extra is added with a 64-bit add, so the result can carry into bit 32.
// Proposed name: ComputeRecordSize_8310FC68 (low confidence: the callers, all
// in 0x8310..0x831D, are unnamed; the type codes look like record/opcode kinds).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kFirstType = 84, kLastType = 125;  // 42 codes
constexpr uint32_t kTypeWithExtraWords = 125;          // adds r6 * 4

// Extra bytes for type codes 84..124 (84 + index); 125 is kTypeWithExtraWords.
constexpr uint8_t kExtra[41] = {
    32, 20, 16, 16, 0, 16, 16, 0, 0, 0,   // 84..93
    12, 0, 20, 24, 0, 24, 24, 0, 0, 4,    // 94..103
    8, 0, 0, 0, 0, 0, 0, 0, 8, 0,         // 104..113
    0, 0, 4, 16, 0, 12, 4, 0, 0, 0,       // 114..123
    8,                                    // 124
};

uint64_t ComputeRecordSize(uint32_t type, uint32_t count, uint32_t extra_words) {
  const uint64_t base = uint32_t((count + 11) << 2);
  const uint32_t index = type - kFirstType;  // unsigned: types below 84 wrap to big numbers
  if (index > kLastType - kFirstType) return base;
  if (type == kTypeWithExtraWords) return base + uint32_t(extra_words << 2);
  return base + kExtra[index];
}

}  // namespace

extern "C" REX_FUNC(rewrite_IndirectCall_8310FC68) {
  ctx.r3.u64 = ComputeRecordSize(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
}

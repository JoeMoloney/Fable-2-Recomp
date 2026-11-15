// Readable rewrite of Ctype_Pred_Bit2_82CA6B10 (guest 0x82CA6B10, 7 PPC
// instructions, 48 callers). Test: python3 tools/theseus/difftest.py Ctype_Pred_Bit2_82CA6B10
//
// C runtime isdigit: looks the character up in the current ctype table
// (16-bit flags per character; MSVC _DIGIT = 0x4). r3 = character; returns
// 0x4 if it is a digit, else 0.
// The table pointer lives in a global at 0x832EF350. Note for the test: the
// harness leaves globals at zero, so this only checks the address arithmetic
// against a zero table (see the pilot table).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kCtypeTablePtr = 0x832EF350;
constexpr uint32_t kDigitFlag = 0x4;

uint32_t IsDigit(uint8_t* base, uint32_t c) {
  const uint32_t table = REX_LOAD_U32(kCtypeTablePtr);
  return REX_LOAD_U16(table + (c << 1)) & kDigitFlag;
}

}  // namespace

extern "C" REX_FUNC(rewrite_Ctype_Pred_Bit2_82CA6B10) {
  ctx.r3.u64 = IsDigit(base, ctx.r3.u32);
}

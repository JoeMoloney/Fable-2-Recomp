// Readable rewrite of sub_829FC128 (guest 0x829FC128, 22 PPC instructions,
// 42 callers). Gate: python3 tools/theseus/gate.py sub_829FC128
//
// Reads entry `index` of an array of 12-byte tagged values
//   r3 = { +0 Entry* entries; +8 u32 index },  Entry = { u32 type; u32 ?; u32 ref }
// and returns *ref (32-bit). Special case: a type-4 entry whose *ref equals the
// global at 0x8349E158 returns the global at 0x8349E164 instead (a default or
// "null object" substitution).
// Proposed name: none yet (callers are unnamed game code 0x8219..0x828E).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kEntries = 0, kIndex = 8;
constexpr uint32_t kEntrySize = 12, kEntryType = 0, kEntryRef = 8;
constexpr int32_t kTypeSubstitute = 4;
constexpr uint32_t kMatchGlobal = 0x8349E158, kReplacementGlobal = 0x8349E164;

uint32_t GetEntryValue(uint8_t* base, uint32_t list) {
  const uint32_t entry = REX_LOAD_U32(list + kEntries) + REX_LOAD_U32(list + kIndex) * kEntrySize;
  const uint32_t ref = REX_LOAD_U32(entry + kEntryRef);
  const uint32_t value = REX_LOAD_U32(ref);
  if (int32_t(REX_LOAD_U32(entry + kEntryType)) == kTypeSubstitute && value == REX_LOAD_U32(kMatchGlobal))
    return REX_LOAD_U32(kReplacementGlobal);
  return value;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_829FC128) {
  ctx.r3.u64 = GetEntryValue(base, ctx.r3.u32);
}

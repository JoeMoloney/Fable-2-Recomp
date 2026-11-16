// Readable rewrite of sub_82D0D8E8 (guest 0x82D0D8E8, 18 PPC instructions, 8 callers).
// Gate: python3 tools/theseus/gate.py sub_82D0D8E8
//
// Looks up a static table of 36-byte records that starts at 0x8201B5E0 and ends
// at the pointer in 0x832F6434: returns the first record whose int id (+0)
// equals r3, else the default record just before the table (0x8201B5B8).
// Addresses come back sign-extended (built with lis/addi).
// Name: unknown (static descriptor lookup by id).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kDefault = 0x8201B5B8, kFirst = 0x8201B5E0, kEndPtr = 0x832F6434;
constexpr uint32_t kRecordSize = 36;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82D0D8E8) {
  const uint32_t end = REX_LOAD_U32(kEndPtr);
  uint32_t found = kDefault;
  for (uint32_t rec = kFirst; rec < end; rec += kRecordSize) {
    if (int32_t(REX_LOAD_U32(rec)) == ctx.r3.s32) {
      found = rec;
      break;
    }
  }
  ctx.r3.s64 = int32_t(found);
}

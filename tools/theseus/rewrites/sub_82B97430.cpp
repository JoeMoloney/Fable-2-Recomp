// Readable rewrite of sub_82B97430 (guest 0x82B97430, 12 PPC instructions, 20 callers).
// Gate: python3 tools/theseus/gate.py sub_82B97430
//
// r3 = object whose first word selects one of its 9936-byte records (stored
// from r3 on). Returns 0 when the record's +96 field is negative, else the
// address of the record's array (+124) element r4 (8-byte elements; 64-bit sum).
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kRecordSize = 9936, kRecordState = 96, kRecordArray = 124;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82B97430) {
  const uint64_t record = uint64_t(REX_LOAD_U32(ctx.r3.u32 + 0)) * kRecordSize + ctx.r3.u64;
  if (int32_t(REX_LOAD_U32(uint32_t(record) + kRecordState)) < 0) {
    ctx.r3.u64 = 0;
    return;
  }
  ctx.r3.u64 = uint64_t(REX_LOAD_U32(uint32_t(record) + kRecordArray)) + (ctx.r4.u32 << 3);
}

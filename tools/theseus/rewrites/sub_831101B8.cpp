// Readable rewrite of sub_831101B8 (guest 0x831101B8, 44 PPC instructions,
// 35 callers). Gate: python3 tools/theseus/gate.py sub_831101B8
//
// A yes/no test (1/0 in r3) on a record r3 = { +0 u32 a; +4 Node* list; +8 u32 flags }:
//   - 0 if the kind in flags bits 7..13 is 83..95;
//   - 0 if flags bit 25 is set and a or list is non-zero;
//   - otherwise 1 unless some node in the list (linked by +8) is "active"
//     (+16 non-zero) and has any of the bits 0x4E000000 set in its +0 word.
// Same code region (0x8310..0x831D) as IndirectCall_8310FC68: probably the same
// record/opcode system. Proposed name: none yet.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kRecordA = 0, kRecordList = 4, kRecordFlags = 8;
constexpr uint32_t kNodeFlags = 0, kNodeNext = 8, kNodeActive = 16;
constexpr uint32_t kKindShift = 7, kKindMask = 0x7F, kKindFirstExcluded = 83, kKindLastExcluded = 95;
constexpr uint32_t kFlagExclusive = 0x2000000;  // bit 25
constexpr uint32_t kNodeBlockingBit = 0x40000000, kNodeBlockingField = 0x0E000000;

bool Check(uint8_t* base, uint32_t record) {
  const uint32_t flags = REX_LOAD_U32(record + kRecordFlags);
  const uint32_t kind = (flags >> kKindShift) & kKindMask;
  if (kind >= kKindFirstExcluded && kind <= kKindLastExcluded) return false;
  if ((flags & kFlagExclusive) &&
      (REX_LOAD_U32(record + kRecordA) != 0 || REX_LOAD_U32(record + kRecordList) != 0))
    return false;
  for (uint32_t node = REX_LOAD_U32(record + kRecordList); node != 0; node = REX_LOAD_U32(node + kNodeNext)) {
    if (REX_LOAD_U32(node + kNodeActive) == 0) continue;
    const uint32_t node_flags = REX_LOAD_U32(node + kNodeFlags);
    if ((node_flags & kNodeBlockingBit) || (node_flags & kNodeBlockingField)) return false;
  }
  return true;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_831101B8) {
  ctx.r3.u64 = Check(base, ctx.r3.u32) ? 1 : 0;
}

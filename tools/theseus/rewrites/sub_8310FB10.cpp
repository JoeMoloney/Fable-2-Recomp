// Readable rewrite of sub_8310FB10 (guest 0x8310FB10, 49 PPC instructions,
// 23 callers). Gate: python3 tools/theseus/gate.py sub_8310FB10
//
// Are two records (r3, r4) compatible? Returns 1/0. Each record is
// { +0 u32 desc; ...; +12 Type* type }, Type = { +8 u32 flags }, and desc packs
//   bits 0..4 a 5-bit id, bits 5.. data, bits 25..27 a count c, bit 30 a flag,
//   bits 13..24 a field compared only when the flag is set.
// Records of different types are compatible only if both types are of kind 120
// (flags bits 7..13) and the flags and (if set) bits 13..24 match.
// Then the ids and counts must match, and the low 2*c data bits (at most 8,
// the compare is on a byte) must match.
// Same family as IndirectCall_8310FC68 and sub_831101B8 (kind field bits 7..13).
// Proposed name: none yet.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kRecordDesc = 0, kRecordType = 12, kTypeFlags = 8;
constexpr uint32_t kKindShift = 7, kKindMask = 0x7F, kKindShared = 120;
constexpr uint32_t kFlagShift = 30;
constexpr uint32_t kFlaggedField = 0x1FFE000;  // bits 13..24
constexpr uint32_t kIdMask = 0x1F, kDataShift = 5, kCountShift = 25, kCountMask = 7;

uint32_t Kind(uint32_t flags) { return (flags >> kKindShift) & kKindMask; }

bool Compatible(uint8_t* base, uint32_t a, uint32_t b) {
  const uint32_t type_a = REX_LOAD_U32(a + kRecordType);
  const uint32_t type_b = REX_LOAD_U32(b + kRecordType);
  if (type_a != type_b) {
    if (Kind(REX_LOAD_U32(type_a + kTypeFlags)) != kKindShared) return false;
    if (Kind(REX_LOAD_U32(type_b + kTypeFlags)) != kKindShared) return false;
    const uint32_t da = REX_LOAD_U32(a + kRecordDesc), db = REX_LOAD_U32(b + kRecordDesc);
    const uint32_t flag_a = (da >> kFlagShift) & 1, flag_b = (db >> kFlagShift) & 1;
    if (flag_a != flag_b) return false;
    if (flag_a && ((da ^ db) & kFlaggedField)) return false;
  }
  const uint32_t da = REX_LOAD_U32(a + kRecordDesc), db = REX_LOAD_U32(b + kRecordDesc);
  if ((da ^ db) & kIdMask) return false;
  const uint32_t count = (da >> kCountShift) & kCountMask;
  if (count != ((db >> kCountShift) & kCountMask)) return false;
  const uint32_t data_mask = (1u << (2 * count)) - 1;
  return uint8_t(((da >> kDataShift) ^ (db >> kDataShift)) & data_mask) == 0;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_8310FB10) {
  ctx.r3.u64 = Compatible(base, ctx.r3.u32, ctx.r4.u32) ? 1 : 0;
}

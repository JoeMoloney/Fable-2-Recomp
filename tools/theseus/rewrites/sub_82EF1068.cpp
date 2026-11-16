// Readable rewrite of sub_82EF1068 (guest 0x82EF1068, 43 PPC instructions,
// 27 callers). Gate: python3 tools/theseus/gate.py sub_82EF1068
//
// Equality of two weak handles r3, r4, each { +0 Slot* slot; +4 u32 generation },
// Slot = { +0 Object* object }, Object = { +4 u32 generation }.
// A handle is stale when its slot or object is null or its generation is newer
// than the object's. Two stale handles are equal; otherwise the handles are
// equal when slot and generation both match. Returns 1/0 in r3.
// Proposed name: WeakHandle_Equals_82EF1068.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kHandleSlot = 0, kHandleGeneration = 4;
constexpr uint32_t kSlotObject = 0, kObjectGeneration = 4;

bool IsStale(uint8_t* base, uint32_t handle) {
  const uint32_t slot = REX_LOAD_U32(handle + kHandleSlot);
  if (slot == 0) return true;
  const uint32_t object = REX_LOAD_U32(slot + kSlotObject);
  if (object == 0) return true;
  return REX_LOAD_U32(handle + kHandleGeneration) > REX_LOAD_U32(object + kObjectGeneration);
}

bool Equals(uint8_t* base, uint32_t a, uint32_t b) {
  if (IsStale(base, a) && IsStale(base, b)) return true;
  return REX_LOAD_U32(a + kHandleSlot) == REX_LOAD_U32(b + kHandleSlot) &&
         REX_LOAD_U32(a + kHandleGeneration) == REX_LOAD_U32(b + kHandleGeneration);
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82EF1068) {
  ctx.r3.u64 = Equals(base, ctx.r3.u32, ctx.r4.u32) ? 1 : 0;
}

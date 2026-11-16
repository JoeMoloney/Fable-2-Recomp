// Readable rewrite of sub_82639068 (guest 0x82639068, 42 PPC instructions, 12 callers).
// Gate: python3 tools/theseus/gate.py sub_82639068
//
// r3 = object { +0 u32 key; +16 Def* def }. Def { +4 flags; +36 u32* heads;
// +48 u32 live count; +52 sparse key array (8-byte slots, 0xFFFFFFFF = empty);
// +56 u32* values (parallel, 4-byte); +68 u32* next }. Returns 0 when the live
// count is 0. With flag bit 3: the length of the chain heads[key] -> next[i] ...
// until 0xFFFFFFFF. Without it: walks the sparse slots until `live count` used
// ones were seen, counting those whose value equals the object's key.
// Name: unknown (a count of entries for a key).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kObjKey = 0, kObjDef = 16;
constexpr uint32_t kDefFlags = 4, kDefHeads = 36, kDefLive = 48, kDefSlots = 52,
                   kDefValues = 56, kDefNext = 68;
constexpr uint32_t kChained = 0x8, kNone = 0xFFFFFFFF;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82639068) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t def = REX_LOAD_U32(obj + kObjDef);
  const uint32_t live = REX_LOAD_U32(def + kDefLive);
  uint64_t count = 0;
  if (live != 0) {
    if (REX_LOAD_U32(def + kDefFlags) & kChained) {
      const uint32_t next = REX_LOAD_U32(def + kDefNext);
      uint32_t i = REX_LOAD_U32(REX_LOAD_U32(def + kDefHeads) + REX_LOAD_U32(obj + kObjKey) * 4);
      for (; i != kNone; i = REX_LOAD_U32(next + i * 4)) ++count;
    } else {
      const uint32_t slots = REX_LOAD_U32(def + kDefSlots);
      uint32_t seen = 0;
      for (uint32_t s = 0; seen < live; ++s) {
        if (REX_LOAD_U32(slots + s * 8) == kNone) continue;
        ++seen;
        if (REX_LOAD_U32(REX_LOAD_U32(def + kDefValues) + s * 4) == REX_LOAD_U32(obj + kObjKey))
          ++count;
      }
    }
  }
  ctx.r3.u64 = count;
}

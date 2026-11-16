// Readable rewrite of sub_82F6AD80 (guest 0x82F6AD80, 12 PPC instructions,
// 66 callers). Gate: python3 tools/theseus/gate.py sub_82F6AD80
//
// Reads a tagged value { u8 type; ...; u32 ref at +4 }: returns the ref (32-bit,
// zero-extended) when the type is 5 or 7, else 0. (For type 7 the original tests
// ref != 0 first and returns 0 otherwise, which is the same value.)
// Proposed name: TaggedValue_GetRef_82F6AD80 (types 5/7 may be table/userdata
// as in Lua; unconfirmed).
#include "fable_2_pch.h"

namespace {

constexpr uint8_t kTypeA = 5, kTypeB = 7;
constexpr uint32_t kTypeOffset = 0, kRefOffset = 4;

uint32_t GetRef(uint8_t* base, uint32_t value) {
  const uint8_t type = REX_LOAD_U8(value + kTypeOffset);
  if (type == kTypeA || type == kTypeB) return REX_LOAD_U32(value + kRefOffset);
  return 0;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82F6AD80) {
  ctx.r3.u64 = GetRef(base, ctx.r3.u32);
}

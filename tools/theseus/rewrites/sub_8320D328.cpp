// Readable rewrite of sub_8320D328 (guest 0x8320D328, 9 PPC instructions, 12 callers).
// Gate: python3 tools/theseus/gate.py sub_8320D328
//
// r4 = object (r3 unused). Returns the object's id (+56); when bit 7 of its
// flags (+228) is set, the id is first remapped through a global u32 table
// (pointer at 0x8335C2A4): table[id].
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kRemapTable = 0x8335C2A4;
constexpr uint32_t kId = 56, kFlags = 228, kRemapped = 0x80;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_8320D328) {
  const uint32_t obj = ctx.r4.u32;
  const uint32_t id = REX_LOAD_U32(obj + kId);
  if (REX_LOAD_U32(obj + kFlags) & kRemapped)
    ctx.r3.u64 = REX_LOAD_U32(REX_LOAD_U32(kRemapTable) + id * 4);
  else
    ctx.r3.u64 = id;
}

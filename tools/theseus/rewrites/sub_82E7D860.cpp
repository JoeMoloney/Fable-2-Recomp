// Readable rewrite of sub_82E7D860 (guest 0x82E7D860, 9 PPC instructions,
// 35 callers). Gate: python3 tools/theseus/gate.py sub_82E7D860
//
// Bounds-checked array get: r3 = array object { +24 u32* items; +32 u32 count },
// r4 = index. Returns items[index] (zero-extended) if index < count, else 0.
// Proposed name: PtrArray_GetAt_82E7D860.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kItems = 24, kCount = 32;

uint32_t GetAt(uint8_t* base, uint32_t array, uint32_t index) {
  if (index >= REX_LOAD_U32(array + kCount)) return 0;
  return REX_LOAD_U32(REX_LOAD_U32(array + kItems) + index * 4);
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82E7D860) {
  ctx.r3.u64 = GetAt(base, ctx.r3.u32, ctx.r4.u32);
}

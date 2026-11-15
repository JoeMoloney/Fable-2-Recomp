// Readable rewrite of GrowAndZeroBuffer_82BC55B8 (guest 0x82BC55B8, 29 PPC
// instructions, 72 callers). Test: python3 tools/theseus/difftest.py GrowAndZeroBuffer_82BC55B8
//
// This is Lua 5.1's lua_settop(L, idx):
//   r3 = lua_State* L, r4 = idx. Lua stack slots (TValue) are 8 bytes, the
//   type tag at +4 (0 = nil).
//   idx >= 0: push nils until top == base + idx, then top = base + idx
//             (this also pops when the stack was higher)
//   idx < 0:  top += idx + 1 slots (-1 = unchanged, -2 = pop one, ...)
// Addresses are 32-bit. L->top and L->base are re-read from memory on every
// step, exactly like the original: in the random test the stack may overlap
// the lua_State itself (it never does in the game).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kTopOffset = 8;    // L->top
constexpr uint32_t kBaseOffset = 12;  // L->base
constexpr uint32_t kSlotSize = 8;     // sizeof(TValue)
constexpr uint32_t kTypeTagOffset = 4;
constexpr uint32_t kNilTag = 0;

void LuaSetTop(uint8_t* base, uint32_t L, int32_t idx) {
  if (idx < 0) {
    const uint32_t top = REX_LOAD_U32(L + kTopOffset);
    REX_STORE_U32(L + kTopOffset, top + uint32_t(idx + 1) * kSlotSize);
    return;
  }
  const uint32_t wanted_bytes = uint32_t(idx) * kSlotSize;
  while (REX_LOAD_U32(L + kTopOffset) < REX_LOAD_U32(L + kBaseOffset) + wanted_bytes) {
    REX_STORE_U32(REX_LOAD_U32(L + kTopOffset) + kTypeTagOffset, kNilTag);  // setnilvalue(top)
    REX_STORE_U32(L + kTopOffset, REX_LOAD_U32(L + kTopOffset) + kSlotSize);  // top++
  }
  REX_STORE_U32(L + kTopOffset, REX_LOAD_U32(L + kBaseOffset) + wanted_bytes);
}

}  // namespace

extern "C" REX_FUNC(rewrite_GrowAndZeroBuffer_82BC55B8) {
  LuaSetTop(base, ctx.r3.u32, ctx.r4.s32);
}

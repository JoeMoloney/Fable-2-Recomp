// Readable rewrite of GetIndexedPointer_82BC5448 (guest 0x82BC5448, 50 PPC instructions, 52 callers).
// Gate: python3 tools/theseus/gate.py GetIndexedPointer_82BC5448
//
// Lua 5.1 index2adr(L = r3, idx = r4): the TValue* for a stack index or pseudo-index.
//   idx > 0:            L->base + (idx - 1), or &luaO_nilobject when at/after L->top
//   -10000 < idx <= 0:  L->top + idx
//   LUA_GLOBALSINDEX    (-10002): &L->l_gt (L + 68)
//   LUA_ENVIRONINDEX    (-10001): L->env = the current C closure's env (stored as a
//                       table, tag 5), returns &L->env (L + 76)
//   LUA_REGISTRYINDEX   (-10000): &G(L)->l_registry (global_State + 92)
//   below: upvalue (-10002 - idx) of the current C closure, or &luaO_nilobject
//   when past its nupvalues.
// This build's lua_State: +8 top, +12 base, +16 l_G, +20 ci; luaO_nilobject at
// 0x82090754. Addresses are 64-bit sums of the register values.
// Proposed name: lua_index2adr_82BC5448.
#include "fable_2_pch.h"

namespace {

constexpr int32_t kRegistryIndex = -10000, kEnvironIndex = -10001, kGlobalsIndex = -10002;
constexpr uint32_t kTop = 8, kBase = 12, kGlobalState = 16, kCallInfo = 20;
constexpr uint32_t kGt = 68, kEnv = 76, kEnvTag = kEnv + 4, kRegistry = 92;
constexpr uint32_t kCiFunc = 4, kClosureNUpvalues = 7, kClosureEnv = 12, kClosureUpvalues = 12;
constexpr uint32_t kTValueSize = 8, kTagTable = 5;
constexpr int64_t kNilObject = int32_t(0x82090754);

uint32_t CurrentClosure(uint8_t* base, uint32_t L) {
  return REX_LOAD_U32(REX_LOAD_U32(REX_LOAD_U32(L + kCallInfo) + kCiFunc));
}

}  // namespace

extern "C" REX_FUNC(rewrite_GetIndexedPointer_82BC5448) {
  const uint32_t L = ctx.r3.u32;
  const int32_t idx = ctx.r4.s32;
  if (idx > 0) {
    const uint64_t o = uint64_t(REX_LOAD_U32(L + kBase)) + (ctx.r4.u32 << 3) - kTValueSize;
    if (uint32_t(o) >= REX_LOAD_U32(L + kTop)) ctx.r3.s64 = kNilObject;
    else ctx.r3.u64 = o;
    return;
  }
  if (idx > kRegistryIndex) {
    ctx.r3.u64 = uint64_t(REX_LOAD_U32(L + kTop)) + (ctx.r4.u32 << 3);
    return;
  }
  switch (idx) {
    case kGlobalsIndex:
      ctx.r3.u64 = ctx.r3.u64 + kGt;
      return;
    case kEnvironIndex: {
      const uint32_t env = REX_LOAD_U32(CurrentClosure(base, L) + kClosureEnv);
      REX_STORE_U32(L + kEnvTag, kTagTable);
      REX_STORE_U32(L + kEnv, env);
      ctx.r3.u64 = ctx.r3.u64 + kEnv;
      return;
    }
    case kRegistryIndex:
      ctx.r3.u64 = uint64_t(REX_LOAD_U32(L + kGlobalState)) + kRegistry;
      return;
    default: {
      const uint32_t closure = CurrentClosure(base, L);
      const uint64_t n = uint64_t(int64_t(kGlobalsIndex)) - ctx.r4.u64;  // upvalue number, 1-based
      if (int32_t(n) > int32_t(REX_LOAD_U8(closure + kClosureNUpvalues))) {
        ctx.r3.s64 = kNilObject;
        return;
      }
      ctx.r3.u64 = uint64_t(uint32_t(n) << 3) + closure + kClosureUpvalues;
    }
  }
}

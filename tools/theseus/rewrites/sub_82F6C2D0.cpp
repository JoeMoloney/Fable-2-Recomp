// Readable rewrite of sub_82F6C2D0 (guest 0x82F6C2D0, 21 PPC instructions,
// 198 callers). Test: python3 tools/theseus/difftest.py sub_82F6C2D0
//
// Scaleform GFx (ActionScript 2): GASFnCall::Arg(int n) returns the n-th
// argument, &env->Stack.Bottom(FirstArgBottomIndex - n), or null past the top.
// Evidence: the callers sit in the GFx code (0x82F0..0x82FB, tagged
// "Gameface UI (Scaleform GFx)"); e.g. 0x82F11560 is Math.abs: Arg(0) ->
// ToNumber -> fabs -> result. Values are 16-byte GASValues in a paged stack,
// 32 per page. Better name: GFx_GASFnCall_Arg.
//   fncall: +24 env (the stack is at env+0), +32 FirstArgBottomIndex
//   stack:  +8 current pointer in the last page, +12 last page start,
//           +24 page table, +28 page count
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kFnCallEnv = 24, kFnCallFirstArg = 32;
constexpr uint32_t kStackPtr = 8, kStackPageStart = 12, kStackPages = 24, kStackPageCount = 28;
constexpr uint32_t kValueSize = 16, kValuesPerPage = 32;

uint32_t FnCallArg(uint8_t* base, uint32_t fncall, uint64_t n) {
  const uint32_t stack = REX_LOAD_U32(fncall + kFnCallEnv);
  const uint32_t index = REX_LOAD_U32(fncall + kFnCallFirstArg) - uint32_t(n);
  // srawi: an arithmetic shift (rounds toward -infinity), not a division; it
  // only differs if the pointer were below the page start, which never happens.
  const int32_t in_last_page =
      int32_t(REX_LOAD_U32(stack + kStackPtr) - REX_LOAD_U32(stack + kStackPageStart)) >> 4;  // / kValueSize
  const uint32_t size = (REX_LOAD_U32(stack + kStackPageCount) - 1) * kValuesPerPage + uint32_t(in_last_page);
  if (index > size) return 0;
  const uint32_t page = REX_LOAD_U32(REX_LOAD_U32(stack + kStackPages) + (index / kValuesPerPage) * 4);
  return page + (index % kValuesPerPage) * kValueSize;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82F6C2D0) {
  ctx.r3.u64 = FnCallArg(base, ctx.r3.u32, ctx.r4.u64);
}

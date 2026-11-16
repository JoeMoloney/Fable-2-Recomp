// Readable rewrite of sub_82ECCFD0 (guest 0x82ECCFD0, 21 PPC instructions, 14 callers).
// Gate: python3 tools/theseus/gate.py sub_82ECCFD0
//
// Walks the sibling list (+64) from r3 (same nodes as sub_82ECD0F0) and returns
// 1 when every node has a descriptor (+20) of kind 5 and both byte flags +48 and
// +56 clear; 0 at the first node that doesn't. An empty list gives 1.
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kDesc = 20, kFlagA = 48, kFlagB = 56, kNext = 64;
constexpr uint32_t kDescKind = 16, kKindRange = 5;

bool NodeOk(uint8_t* base, uint32_t node) {
  const uint32_t desc = REX_LOAD_U32(node + kDesc);
  return desc != 0 && REX_LOAD_U32(desc + kDescKind) == kKindRange &&
         REX_LOAD_U8(node + kFlagB) == 0 && REX_LOAD_U8(node + kFlagA) == 0;
}
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82ECCFD0) {
  for (uint32_t node = ctx.r3.u32; node != 0; node = REX_LOAD_U32(node + kNext)) {
    if (!NodeOk(base, node)) {
      ctx.r3.u64 = 0;
      return;
    }
  }
  ctx.r3.u64 = 1;
}

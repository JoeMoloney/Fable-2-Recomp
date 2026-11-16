// Readable rewrite of sub_82ECD0F0 (guest 0x82ECD0F0, 20 PPC instructions, 22 callers).
// Gate: python3 tools/theseus/gate.py sub_82ECD0F0
//
// r3 = node, r4 = address. Climbs to the root through the parent link at +60,
// then walks the sibling list (+64) from there and returns the first node whose
// descriptor (+20) has kind 5 (+16) and a range [start (+20), start + size (+24)]
// (u32, inclusive) containing r4. Returns 0 when none does. If the start node
// has no parent and matches, r3 comes back unchanged (all 64 bits).
// Name: unknown (find the region containing an address).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kDesc = 20, kParent = 60, kNext = 64;
constexpr uint32_t kDescKind = 16, kDescStart = 20, kDescSize = 24;
constexpr uint32_t kKindRange = 5;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82ECD0F0) {
  uint64_t node = ctx.r3.u64;
  while (uint32_t parent = REX_LOAD_U32(uint32_t(node) + kParent)) node = parent;
  const uint32_t addr = ctx.r4.u32;
  do {
    const uint32_t desc = REX_LOAD_U32(uint32_t(node) + kDesc);
    if (REX_LOAD_U32(desc + kDescKind) == kKindRange) {
      const uint32_t start = REX_LOAD_U32(desc + kDescStart);
      if (addr >= start && addr <= start + REX_LOAD_U32(desc + kDescSize)) break;
    }
    node = REX_LOAD_U32(uint32_t(node) + kNext);
  } while (node != 0);
  ctx.r3.u64 = node;
}

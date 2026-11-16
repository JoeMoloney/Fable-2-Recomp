// Readable rewrite of FindChainRoot_82A596F0 (guest 0x82A596F0, 41 PPC
// instructions, 62 callers / 183 call sites).
// Test: python3 tools/theseus/difftest.py FindChainRoot_82A596F0
//
// This is MSVC's checked std::map / std::set iterator operator++ (_Tree::_Inc):
// it moves the iterator at r3 to the in-order successor node.
//   iterator: +0 owning container (debug-iterator proxy), +4 current node
//   node:     +0 left, +4 parent, +8 right, +17 isnil (1 only on the head sentinel)
// Debug checks (twi 31 = trap 22, which only logs a warning and carries on):
//   - iterator has no container: trap, then increment anyway
//   - iterator is end() (the head): trap and return unchanged
// The name was a guess from its shape; a better name is MapIterator_Increment.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kLeft = 0, kParent = 4, kRight = 8, kIsNil = 17;
constexpr uint32_t kIterContainer = 0, kIterNode = 4;
constexpr uint16_t kTrapAssert = 22;

bool IsNil(uint8_t* base, uint32_t node) { return REX_LOAD_U8(node + kIsNil) != 0; }

void MapIteratorIncrement(PPCContext& ctx, uint8_t* base, uint32_t it) {
  if (REX_LOAD_U32(it + kIterContainer) == 0) ppc_trap(ctx, base, kTrapAssert);
  uint32_t node = REX_LOAD_U32(it + kIterNode);
  if (IsNil(base, node)) {  // ++end()
    ppc_trap(ctx, base, kTrapAssert);
    return;
  }
  uint32_t right = REX_LOAD_U32(node + kRight);
  if (!IsNil(base, right)) {
    // Successor is the leftmost node of the right subtree.
    node = right;
    while (!IsNil(base, REX_LOAD_U32(node + kLeft))) node = REX_LOAD_U32(node + kLeft);
  } else {
    // Climb while we are a right child; the first parent we reach from its
    // left side is the successor (or the head, i.e. end()).
    uint32_t parent = REX_LOAD_U32(node + kParent);
    while (!IsNil(base, parent) && node == REX_LOAD_U32(parent + kRight)) {
      node = parent;
      parent = REX_LOAD_U32(node + kParent);
    }
    node = parent;
  }
  REX_STORE_U32(it + kIterNode, node);
}

}  // namespace

extern "C" REX_FUNC(rewrite_FindChainRoot_82A596F0) {
  MapIteratorIncrement(ctx, base, ctx.r3.u32);
}

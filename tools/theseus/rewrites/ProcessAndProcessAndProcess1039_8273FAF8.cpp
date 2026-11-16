// Readable rewrite of ProcessAndProcessAndProcess1039_8273FAF8 (guest 0x8273FAF8, 41 PPC instructions, 70 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1039_8273FAF8
//
// MSVC checked std::map / std::set iterator operator++ (_Tree::_Inc), the same
// code as FindChainRoot_82A596F0 for a node type whose isnil byte is at +29:
//   iterator: +0 owning container (debug-iterator proxy), +4 current node
//   node:     +0 left, +4 parent, +8 right, +29 isnil (1 only on the head)
// Debug checks (trap 22 only logs): no container -> trap, then increment anyway;
// ++end() -> trap and return unchanged.
// Proposed name: MapIterator_Increment_8273FAF8 (evidence.py: std::_Tree helper).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kLeft = 0, kParent = 4, kRight = 8, kIsNil = 29;
constexpr uint32_t kIterContainer = 0, kIterNode = 4;
constexpr uint16_t kTrapAssert = 22;  // gate: equivalent: harness counts traps, not codes; the code only changes the log text

bool IsNil(uint8_t* base, uint32_t node) { return REX_LOAD_U8(node + kIsNil) != 0; }

}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1039_8273FAF8) {
  const uint32_t it = ctx.r3.u32;
  if (REX_LOAD_U32(it + kIterContainer) == 0) ppc_trap(ctx, base, kTrapAssert);
  uint32_t node = REX_LOAD_U32(it + kIterNode);
  if (IsNil(base, node)) {  // ++end()
    ppc_trap(ctx, base, kTrapAssert);
    return;
  }
  const uint32_t right = REX_LOAD_U32(node + kRight);
  if (!IsNil(base, right)) {
    // Successor: the leftmost node of the right subtree.
    node = right;
    while (!IsNil(base, REX_LOAD_U32(node + kLeft))) node = REX_LOAD_U32(node + kLeft);
  } else {
    // Climb while we are a right child; the first parent reached from its left
    // side is the successor (or the head, i.e. end()).
    uint32_t parent = REX_LOAD_U32(node + kParent);
    while (!IsNil(base, parent) && node == REX_LOAD_U32(parent + kRight)) {
      node = parent;
      parent = REX_LOAD_U32(node + kParent);
    }
    node = parent;
  }
  REX_STORE_U32(it + kIterNode, node);
}

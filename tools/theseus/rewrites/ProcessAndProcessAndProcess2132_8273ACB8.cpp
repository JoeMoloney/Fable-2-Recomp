// Readable rewrite of ProcessAndProcessAndProcess2132_8273ACB8 (guest 0x8273ACB8, 50 PPC instructions, 33 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess2132_8273ACB8
//
// MSVC checked std::map / std::set iterator operator-- (_Tree::_Dec) for a node
// type whose isnil byte is at +29:
//   iterator: +0 owning container (debug-iterator proxy), +4 current node
//   node:     +0 left, +4 parent, +8 right, +29 isnil (1 only on the head)
// --end() moves to the rightmost node (head->right) and traps if the tree is
// empty. No container -> trap, then decrement anyway. The original ends with a
// second "is the result end()" check that can't fire (every node it can hold
// was checked non-nil); it is left out.
// Proposed name: MapIterator_Decrement_8273ACB8 (evidence.py: std::_Tree helper).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kLeft = 0, kParent = 4, kRight = 8, kIsNil = 29;
constexpr uint32_t kIterContainer = 0, kIterNode = 4;
constexpr uint16_t kTrapAssert = 22;  // gate: equivalent: harness counts traps, not codes; the code only changes the log text

bool IsNil(uint8_t* base, uint32_t node) { return REX_LOAD_U8(node + kIsNil) != 0; }

}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess2132_8273ACB8) {
  const uint32_t it = ctx.r3.u32;
  if (REX_LOAD_U32(it + kIterContainer) == 0) ppc_trap(ctx, base, kTrapAssert);
  uint32_t node = REX_LOAD_U32(it + kIterNode);
  if (IsNil(base, node)) {  // --end(): the rightmost node
    node = REX_LOAD_U32(node + kRight);
    REX_STORE_U32(it + kIterNode, node);
    if (IsNil(base, node)) ppc_trap(ctx, base, kTrapAssert);  // empty tree
    return;
  }
  const uint32_t left = REX_LOAD_U32(node + kLeft);
  if (!IsNil(base, left)) {
    // Predecessor: the rightmost node of the left subtree.
    node = left;
    while (!IsNil(base, REX_LOAD_U32(node + kRight))) node = REX_LOAD_U32(node + kRight);
    REX_STORE_U32(it + kIterNode, node);
    return;
  }
  // Climb while we are a left child (the iterator follows each step).
  uint32_t parent = REX_LOAD_U32(node + kParent);
  while (!IsNil(base, parent) && node == REX_LOAD_U32(parent + kLeft)) {
    node = parent;
    REX_STORE_U32(it + kIterNode, node);
    parent = REX_LOAD_U32(node + kParent);
  }
  REX_STORE_U32(it + kIterNode, parent);
}

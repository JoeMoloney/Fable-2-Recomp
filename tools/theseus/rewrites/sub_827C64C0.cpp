// Readable rewrite of sub_827C64C0 (guest 0x827C64C0, 30 PPC instructions, 30 callers).
// Gate: python3 tools/theseus/gate.py sub_827C64C0
//
// MSVC std::_Tree::_Rrotate(where = r4) on the tree at r3 (+4 head; the
// head's parent is the root). Node layout: +0 left, +4 parent, +8 right,
// +25 isnil. A right rotation: where's left child takes its place.
// The loads and stores keep the original's order (it matters if nodes alias,
// e.g. rotating the head of an empty tree).
// Proposed name: Tree_Rrotate_827C64C0 (evidence.py: std::_Tree relink shape).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kLeft = 0, kParent = 4, kRight = 8, kIsNil = 25;
constexpr uint32_t kHead = 4;
// right rotation: the child that moves up, and the side it hands over.
constexpr uint32_t kUp = kLeft, kOver = kRight;

bool IsNil(uint8_t* base, uint32_t node) { return REX_LOAD_U8(node + kIsNil) != 0; }

}  // namespace

extern "C" REX_FUNC(rewrite_sub_827C64C0) {
  const uint32_t tree = ctx.r3.u32, where = ctx.r4.u32;
  const uint32_t pnode = REX_LOAD_U32(where + kUp);
  REX_STORE_U32(where + kUp, REX_LOAD_U32(pnode + kOver));
  const uint32_t moved = REX_LOAD_U32(pnode + kOver);
  if (!IsNil(base, moved)) REX_STORE_U32(moved + kParent, where);
  REX_STORE_U32(pnode + kParent, REX_LOAD_U32(where + kParent));
  const uint32_t head = REX_LOAD_U32(tree + kHead);
  if (where == REX_LOAD_U32(head + kParent)) {
    REX_STORE_U32(head + kParent, pnode);  // where was the root
  } else {
    const uint32_t parent = REX_LOAD_U32(where + kParent);
    if (where == REX_LOAD_U32(parent + kOver))
      REX_STORE_U32(parent + kOver, pnode);
    else
      REX_STORE_U32(parent + kUp, pnode);
  }
  REX_STORE_U32(pnode + kOver, where);
  REX_STORE_U32(where + kParent, pnode);
}

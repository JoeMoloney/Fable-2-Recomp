// Readable rewrite of FindTreeLeaf_825CB448 (guest 0x825CB448, 39 PPC
// instructions, 51 callers / 56 call sites).
// Test: python3 tools/theseus/difftest.py FindTreeLeaf_825CB448
//
// This is MSVC std::map<uint32_t, V>::find(key) where V is 4 bytes:
//   r3 = hidden pointer to the returned iterator {+0 map, +4 node}
//   r4 = the map (+4 head sentinel), r5 = const uint32_t* key
//   node: +0 left, +4 parent, +8 right, +12 key, +16 value, +21 isnil
// lower_bound walk from the root, then: found if the bound is not end() and
// !(key < bound.key); otherwise returns end() (the head).
// The original also builds the iterator in the red zone below r1 (dead after
// return); the rewrite does not. A better name is MapU32_Find.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kLeft = 0, kParent = 4, kRight = 8, kKey = 12, kIsNil = 21;
constexpr uint32_t kMapHead = 4;

void MapFind(uint8_t* base, uint32_t result, uint32_t map, uint32_t key_ptr) {
  const uint32_t head = REX_LOAD_U32(map + kMapHead);
  uint32_t bound = head;  // first node with node.key >= key
  for (uint32_t node = REX_LOAD_U32(head + kParent); REX_LOAD_U8(node + kIsNil) == 0;) {
    if (REX_LOAD_U32(node + kKey) < REX_LOAD_U32(key_ptr)) {
      node = REX_LOAD_U32(node + kRight);
    } else {
      bound = node;
      node = REX_LOAD_U32(node + kLeft);
    }
  }
  const bool found = bound != head && !(REX_LOAD_U32(key_ptr) < REX_LOAD_U32(bound + kKey));
  REX_STORE_U32(result + 0, map);
  REX_STORE_U32(result + 4, found ? bound : head);
}

}  // namespace

extern "C" REX_FUNC(rewrite_FindTreeLeaf_825CB448) {
  MapFind(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

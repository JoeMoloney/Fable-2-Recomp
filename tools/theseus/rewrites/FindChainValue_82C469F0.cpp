// Readable rewrite of FindChainValue_82C469F0 (guest 0x82C469F0, 16 PPC
// instructions, 24 callers). Gate: python3 tools/theseus/gate.py FindChainValue_82C469F0
//
// Value of the last node of a singly linked list that ends at a sentinel:
//   r3 = { +0 Node* head; +4 Node* end }, Node = { +0 Node* next; ...; +16 value }.
// Walks from *head until a node whose next is `end` and returns its +16 word
// (zero-extended); returns 0 when the list is empty (*head == end).
// Proposed name: List_LastValue_82C469F0.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kListHead = 0, kListEnd = 4;
constexpr uint32_t kNodeNext = 0, kNodeValue = 16;

uint32_t LastValue(uint8_t* base, uint32_t list) {
  const uint32_t end = REX_LOAD_U32(list + kListEnd);
  uint32_t node = REX_LOAD_U32(REX_LOAD_U32(list + kListHead) + kNodeNext);
  if (node == end) return 0;
  for (uint32_t next; (next = REX_LOAD_U32(node + kNodeNext)) != end;) node = next;
  return REX_LOAD_U32(node + kNodeValue);
}

}  // namespace

extern "C" REX_FUNC(rewrite_FindChainValue_82C469F0) {
  ctx.r3.u64 = LastValue(base, ctx.r3.u32);
}

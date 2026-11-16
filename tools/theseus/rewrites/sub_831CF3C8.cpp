// Readable rewrite of sub_831CF3C8 (guest 0x831CF3C8, 14 PPC instructions, 15 callers).
// Gate: python3 tools/theseus/gate.py sub_831CF3C8
//
// Intrusive singly linked list: each element embeds its link at +40, and the
// link word at element+36 holds the next element's link address (element + 40),
// with bit 0 set as an end tag. r3 = start element (bit 0 ignored), r4 = element
// to find. Returns 1 if r4 follows the start in the list, else 0 (end tag or a
// null element reached).
// Name: unknown (List_Contains-like).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kLinkWord = 36, kLinkOffset = 40;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_831CF3C8) {
  uint32_t elem = ctx.r3.u32 & ~1u;
  for (;;) {
    const uint32_t link = REX_LOAD_U32(elem + kLinkWord);
    if (link & 1) break;
    elem = link - kLinkOffset;
    if (elem == 0) break;
    if (elem == ctx.r4.u32) {
      ctx.r3.u64 = 1;
      return;
    }
  }
  ctx.r3.u64 = 0;
}

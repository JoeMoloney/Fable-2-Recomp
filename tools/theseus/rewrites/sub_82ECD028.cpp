// Readable rewrite of sub_82ECD028 (guest 0x82ECD028, 12 PPC instructions, 10 callers).
// Gate: python3 tools/theseus/gate.py sub_82ECD028
//
// Walks the sibling list (+64) from r3 (same nodes as sub_82ECD0F0) and returns
// 1 if any node has the byte flag at +56 set, else 0.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_82ECD028) {
  for (uint32_t node = ctx.r3.u32; node != 0; node = REX_LOAD_U32(node + 64)) {
    if (REX_LOAD_U8(node + 56) != 0) {
      ctx.r3.u64 = 1;
      return;
    }
  }
  ctx.r3.u64 = 0;
}

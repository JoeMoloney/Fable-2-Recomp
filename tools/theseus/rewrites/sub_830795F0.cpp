// Readable rewrite of sub_830795F0 (guest 0x830795F0, 19 PPC instructions, 51 callers).
// Gate: python3 tools/theseus/gate.py sub_830795F0
//
// Initialises a 64-byte struct at r3: all zero except the words at +48 and +52,
// which are -1 (e.g. "no index" markers).
// Name: unknown (a constructor / Reset).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_830795F0) {
  const uint32_t obj = ctx.r3.u32;
  for (uint32_t off = 0; off < 64; off += 4)
    REX_STORE_U32(obj + off, (off == 48 || off == 52) ? 0xFFFFFFFF : 0);
}

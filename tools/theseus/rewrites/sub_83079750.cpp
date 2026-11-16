// Readable rewrite of sub_83079750 (guest 0x83079750, 20 PPC instructions, 8 callers).
// Gate: python3 tools/theseus/gate.py sub_83079750
//
// r3 = object; returns 1 when the top 12 bits of its first word are one of
// 0x200, 0x201, 0x204, 0x205 or 0x500 (a class/type id in the high bits), else 0.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_83079750) {
  switch (REX_LOAD_U32(ctx.r3.u32 + 0) & 0xFFF00000) {
    case 0x20000000: case 0x20100000: case 0x20400000: case 0x20500000: case 0x50000000:
      ctx.r3.u64 = 1;
      return;
    default:
      ctx.r3.u64 = 0;
  }
}

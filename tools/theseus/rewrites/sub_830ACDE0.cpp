// Readable rewrite of sub_830ACDE0 (guest 0x830ACDE0, 21 PPC instructions, 17 callers).
// Gate: python3 tools/theseus/gate.py sub_830ACDE0
//
// Returns 1 when the low 32 bits of r3 are one of six fixed codes
// (0x60000000, 0x60500000, 0x60700000, 0x60A00000, 0x60F00000, 0x72E00000),
// else 0.
// Name: unknown (a set of ids/addresses with the low 20 bits clear).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_830ACDE0) {
  switch (ctx.r3.u32) {
    case 0x60500000: case 0x60A00000: case 0x60000000:
    case 0x72E00000: case 0x60700000: case 0x60F00000:
      ctx.r3.u64 = 1;
      return;
    default:
      ctx.r3.u64 = 0;
  }
}

// Readable rewrite of sub_830E71D8 (guest 0x830E71D8, 10 PPC instructions, 11 callers).
// Gate: python3 tools/theseus/gate.py sub_830E71D8
//
// D3DCOLOR_RGBA(r = r3, g = r4, b = r5, a = r6): packs the low bytes into
// 0xAARRGGBB.
// Proposed name: D3DCOLOR_RGBA_830E71D8 (argument order r, g, b, a; alpha on top).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_830E71D8) {
  const uint32_t r = ctx.r3.u32 & 0xFF, g = ctx.r4.u32 & 0xFF;
  const uint32_t b = ctx.r5.u32 & 0xFF, a = ctx.r6.u32 & 0xFF;
  ctx.r3.u64 = (a << 24) | (r << 16) | (g << 8) | b;
}

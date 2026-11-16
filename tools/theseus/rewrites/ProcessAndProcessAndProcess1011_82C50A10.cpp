// Readable rewrite of ProcessAndProcessAndProcess1011_82C50A10 (guest 0x82C50A10,
// 6 PPC instructions, 7 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1011_82C50A10
//
// r3 = object; returns byte +1 of the record its +92 field points at, or 0 when
// there is none.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1011_82C50A10) {
  const uint32_t rec = REX_LOAD_U32(ctx.r3.u32 + 92);
  ctx.r3.u64 = rec ? REX_LOAD_U8(rec + 1) : 0;
}

// Readable rewrite of DispatchByTypeCode2_821FABA0 (guest 0x821FABA0, 35 PPC
// instructions, 10 callers). Gate: python3 tools/theseus/gate.py DispatchByTypeCode2_821FABA0
//
// r3 = typed object; t = low nibble of its first word. Returns a refined type
// code: t == 3 splits on the 2-bit field at bits 9..10 of +48 (3 -> 18, 2 -> 17,
// 0 -> 20, 1 -> 19 if flag 0x400 of +32, else 3); t == 4 with flag 0x40000000
// becomes 16 when the referenced object (+24) has (word +48 & 0x600) == 0x400;
// any other case returns t.
// Name kept (DispatchByTypeCode2).
#include "fable_2_pch.h"

namespace {
uint32_t RefineType3(uint8_t* base, uint32_t obj) {
  switch ((REX_LOAD_U32(obj + 48) >> 9) & 3) {
    case 3: return 18;
    case 2: return 17;
    case 0: return 20;
    default: return (REX_LOAD_U32(obj + 32) & 0x400) ? 19 : 3;
  }
}
}  // namespace

extern "C" REX_FUNC(rewrite_DispatchByTypeCode2_821FABA0) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t w = REX_LOAD_U32(obj + 0);
  uint32_t type = w & 0xF;
  if (type == 3) {
    type = RefineType3(base, obj);
  } else if (type == 4 && (w & 0x40000000)) {
    if ((REX_LOAD_U32(REX_LOAD_U32(obj + 24) + 48) & 0x600) == 0x400) type = 16;
  }
  ctx.r3.u64 = type;
}

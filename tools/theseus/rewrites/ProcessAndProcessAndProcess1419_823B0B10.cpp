// Readable rewrite of ProcessAndProcessAndProcess1419_823B0B10 (guest 0x823B0B10,
// 9 PPC instructions, 11 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1419_823B0B10
//
// std::vector<T*>::size() for MSVC's layout (+4 first, +8 last): 0 when first is
// null, else (last - first) / 4 as a 32-bit arithmetic shift, sign-extended.
// Proposed name: std_vector4_size_823B0B10 (evidence.py: vector helper).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1419_823B0B10) {
  const uint32_t vec = ctx.r3.u32;
  const uint32_t first = REX_LOAD_U32(vec + 4);
  if (first == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  ctx.r3.s64 = int32_t(REX_LOAD_U32(vec + 8) - first) >> 2;
}

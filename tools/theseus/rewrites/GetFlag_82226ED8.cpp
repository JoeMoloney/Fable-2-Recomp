// Readable rewrite of GetFlag_82226ED8 (guest 0x82226ED8, 9 PPC instructions, 8 callers).
// Gate: python3 tools/theseus/gate.py GetFlag_82226ED8
//
// Returns the byte flag at 0x834968C5 when the global at 0x83496920 (an
// "initialised" pointer/handle) is non-zero, else 0.
// Name kept (GetFlag).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kOwner = 0x83496920, kFlag = 0x834968C5;
}  // namespace

extern "C" REX_FUNC(rewrite_GetFlag_82226ED8) {
  ctx.r3.u64 = REX_LOAD_U32(kOwner) ? REX_LOAD_U8(kFlag) : 0;
}

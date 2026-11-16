// Readable rewrite of SwitchDispatch_8250B778 (guest 0x8250B778, 15 PPC
// instructions, 25 callers, 10 of them Lua bindings).
// Gate: python3 tools/theseus/gate.py SwitchDispatch_8250B778
//
// Maps a kind (r3, low 32 bits) to a group: 2..4 -> 1, 5..8 -> 2, else 0.
// Proposed name: none yet (a small enum-to-category switch).
#include "fable_2_pch.h"

namespace {

int64_t KindGroup(uint32_t kind) {
  if (kind >= 2 && kind <= 4) return 1;
  if (kind >= 5 && kind <= 8) return 2;
  return 0;
}

}  // namespace

extern "C" REX_FUNC(rewrite_SwitchDispatch_8250B778) {
  ctx.r3.s64 = KindGroup(ctx.r3.u32);
}

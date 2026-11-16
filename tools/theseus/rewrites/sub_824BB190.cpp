// Readable rewrite of sub_824BB190 (guest 0x824BB190, 20 PPC instructions, 10 callers).
// Gate: python3 tools/theseus/gate.py sub_824BB190
//
// r3 = object or null. Returns 1 when the object exists, has flag 0x40 in the
// byte at +144 and bit 0 of the u16 at +56 clear; else 0.
// Name: unknown.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_824BB190) {
  const uint32_t obj = ctx.r3.u32;
  const bool yes = obj != 0 && (REX_LOAD_U8(obj + 144) & 0x40) && !(REX_LOAD_U16(obj + 56) & 1);
  ctx.r3.u64 = yes ? 1 : 0;
}

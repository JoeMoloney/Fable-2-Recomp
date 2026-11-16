// Readable rewrite of ToUpper_SdkRuntime_821EEB10 (guest 0x821EEB10, 6 PPC
// instructions, 15 callers). Gate: python3 tools/theseus/gate.py ToUpper_SdkRuntime_821EEB10
//
// ASCII toupper: if the low 32 bits of r3, as a signed int, are 'a'..'z',
// subtracts 32 from the whole 64-bit register; otherwise r3 is unchanged.
// Name confirmed by the code (already named ToUpper).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ToUpper_SdkRuntime_821EEB10) {
  const int32_t c = ctx.r3.s32;
  if (c >= 'a' && c <= 'z') ctx.r3.s64 -= 'a' - 'A';
}

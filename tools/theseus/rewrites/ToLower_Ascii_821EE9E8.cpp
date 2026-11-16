// Readable rewrite of ToLower_Ascii_821EE9E8 (guest 0x821EE9E8, 6 PPC
// instructions, 22 callers). Gate: python3 tools/theseus/gate.py ToLower_Ascii_821EE9E8
//
// ASCII tolower: if the low 32 bits of r3, as a signed int, are 'A'..'Z', adds 32
// to the whole 64-bit register; otherwise r3 is returned unchanged.
// Name confirmed by the code (already named ToLower_Ascii).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ToLower_Ascii_821EE9E8) {
  const int32_t c = ctx.r3.s32;
  if (c >= 'A' && c <= 'Z') ctx.r3.s64 += 'a' - 'A';
}

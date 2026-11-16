// Readable rewrite of ToLower_Ascii_8217A980 (guest 0x8217A980, 10 PPC
// instructions, 16 callers). Gate: python3 tools/theseus/gate.py ToLower_Ascii_8217A980
//
// Wide-char ASCII towlower: c = low 16 bits of r3. 0xFFFF (WEOF) returns r3
// unchanged (all 64 bits); otherwise returns c (zero-extended), with 'A'..'Z'
// lowered.
// Name kept (ToLower_Ascii); proposed towlower_ascii_8217A980.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ToLower_Ascii_8217A980) {
  const uint32_t c = ctx.r3.u32 & 0xFFFF;
  if (c == 0xFFFF) return;
  ctx.r3.u64 = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

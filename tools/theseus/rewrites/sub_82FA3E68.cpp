// Readable rewrite of sub_82FA3E68 (guest 0x82FA3E68, 20 PPC instructions, 18 callers).
// Gate: python3 tools/theseus/gate.py sub_82FA3E68
//
// Gregorian leap-year test on the low 32 bits of r3 (signed, C remainders):
//   return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
// Proposed name: IsLeapYear_82FA3E68 (the 4/100/400 rule).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_sub_82FA3E68) {
  const int32_t year = ctx.r3.s32;
  ctx.r3.u64 = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
}

// Readable rewrite of ComputeWrapped_8222C3E8 (guest 0x82230000-ish, 15 PPC
// instructions, 77 callers). Test: python3 tools/theseus/difftest.py ComputeWrapped_8222C3E8
//
// This is floor(x) for a double in f1, done the PowerPC way: truncate through a
// 64-bit integer (fctidz/fcfid), subtract 1.0 if that went up (negative
// non-integers), and select with fsel so that |x| > 1e18 (already integral, and
// the int64 conversion would overflow), NaN and +-0 come back unchanged.
// That is exactly std::floor, including the sign of zero, except that a
// signalling NaN comes back unchanged (fsel copies bits; std::floor would quiet
// it). Better name: Floor. The constants 1.0 (0x82000CB0) and 1e18 (0x82000CD8)
// are not needed: std::floor already returns |x| > 1e18, +-0 and inf unchanged.
#include "fable_2_pch.h"

#include <cmath>

extern "C" REX_FUNC(rewrite_ComputeWrapped_8222C3E8) {
  ctx.fpscr.disableFlushMode();  // same FPU mode change as the original
  const double x = ctx.f1.f64;
  if (std::isnan(x)) return;  // f1 unchanged, signalling NaNs too
  ctx.f1.f64 = std::floor(x);
}

// Readable rewrite of ComputeSpecialFunction_82239E88 (guest 0x82239E88, 56 PPC
// instructions, 158 callers). Test: python3 tools/theseus/difftest.py ComputeSpecialFunction_82239E88
//
// This is cos(x) for a double in f1 (the C runtime's, Cephes/fdlibm style):
//   cos(x) = sin(|x| + pi/2). n = round((|x| + pi/2) / pi), xn = n - 0.5,
//   r = |x| - xn*pi (Cody-Waite: pi split into a high and a low part, fused),
//   sin(r) by an odd degree-17 polynomial, negated when n is odd.
//   cos(+-0) = 1.0 exactly; |x| + pi/2 >= ~2.2e8 returns the default NaN.
// Every multiply-add below is fused like the PowerPC fmadd/fnmsub, in the same
// order, so the result is bit-exact (NaN signs included). Better name: Cos.
// Constants are from the table at 0x832EEA60 (see the .rules file).
#include "fable_2_pch.h"

#include <bit>
#include <climits>
#include <cmath>

namespace {

// Exact bit patterns from the table (decimal values in the comments).
constexpr double Bits(uint64_t b) { return std::bit_cast<double>(b); }
constexpr double kHalfPi = Bits(0x3FF921FB54442D18);   // pi/2
constexpr double kInvPi = Bits(0x3FD45F306DC9C883);    // 1/pi
constexpr double kHugeArg = Bits(0x41AA39DE00000000);  // 2.2e8
constexpr double kPiHigh = Bits(0x400921FB54400000);   // 3.1415926534682512 (pi, top bits)
constexpr double kPiLow = Bits(0x3DE0B4611A600000);    // 1.2154201012607932e-10 (pi - kPiHigh)
// sin(r) = r * (1 + r^2*(S1 + r^2*(S2 + ... + r^2*S8)))
constexpr double kSin[8] = {
    Bits(0xBFC5555555555555),  // -1/3!
    Bits(0x3F811111111110B0),  // +1/5!
    Bits(0xBF2A01A01A013E1A),  // -1/7!
    Bits(0x3EC71DE3A524F063),  // +1/9!
    Bits(0xBE5AE6454B5DC0AB),  // -1/11!
    Bits(0x3DE6123C686AD430),  // +1/13!
    Bits(0xBD6AE420DC08499C),  // -1/15!
    Bits(0x3CE880FF6993DF95),  // +1/17!
};

// PowerPC fctid / fctidz: double -> int64, saturating, NaN -> INT64_MIN.
int64_t PpcFctid(double v) {   // rounds with the current mode (nearest)
  if (std::isnan(v)) return INT64_MIN;
  if (v > double(LLONG_MAX)) return LLONG_MAX;
  return simde_mm_cvtsd_si64(simde_mm_load_sd(&v));
}
int64_t PpcFctidz(double v) {  // truncates
  if (std::isnan(v)) return INT64_MIN;
  if (v > double(LLONG_MAX)) return LLONG_MAX;
  return simde_mm_cvttsd_si64(simde_mm_load_sd(&v));
}

// PowerPC fnmsub: -(a*b - c), fused.
double NegMulSub(double a, double b, double c) { return -std::fma(a, b, -c); }

double Cos(double x) {
  const double ax = Bits(std::bit_cast<uint64_t>(x) & ~(1ull << 63));  // fabs, NaN bits kept
  const double shifted = kHalfPi + ax;
  const double n = double(PpcFctid(kInvPi * shifted));
  const double xn = n - 0.5;
  const bool odd = (PpcFctidz(n) & 1) != 0;
  double r = NegMulSub(kPiHigh, xn, ax);  // ax - xn*pi_high
  r = NegMulSub(kPiLow, xn, r);           //    - xn*pi_low
  const double r2 = r * r;
  double p = std::fma(kSin[7], r2, kSin[6]);
  for (int i = 5; i >= 0; --i) p = std::fma(p, r2, kSin[i]);
  p = std::fma(p, r2, 1.0);
  double s = p * r;
  if (odd) s = -s;

  if (ax == 0.0) return 1.0;
  return shifted - kHugeArg >= 0.0 ? Bits(0xFFF8000000000000ull) : s;
}

}  // namespace

extern "C" REX_FUNC(rewrite_ComputeSpecialFunction_82239E88) {
  ctx.fpscr.disableFlushMode();  // same FPU mode change as the original
  ctx.f1.f64 = Cos(ctx.f1.f64);
}

// Readable rewrite of NormalizeVector_82260BC8 (guest 0x82260BC8, 24 PPC
// instructions, 177 callers). Test: python3 tools/theseus/difftest.py NormalizeVector_82260BC8
//
// float NormalizeVec3InPlace(Vec4* v /* r3, 16-byte aligned */, double min_len /* f1 */):
//   len = sqrt(x*x + y*y + z*z)          (VMX128 vmsum3fp, denormals flushed)
//   if (len < min_len) { *v = {0,0,0,0}; return 0.0f; }
//   *v *= 1.0f / len   (all four lanes, w too, denormals flushed); return len
// Exactness notes: the dot product uses the same host instruction as the
// recompiler (dpps): with two NaN inputs the NaN that survives depends on the
// add order, and the compiler may reorder a plain C++ x*x + y*y + z*z;
// sqrt is taken in double and rounded to float; 1/len is a double division
// rounded to float. Constants 0.0f (0x820994B4) and 1.0f (0x820994C0) are
// written in here. The original also stores the dot product and 1/len in the
// red zone below r1 (dead after return); this version does not.
#include "fable_2_pch.h"

#include <cmath>

namespace {

float LoadF32(uint8_t* base, uint32_t addr) {
  const uint32_t bits = REX_LOAD_U32(addr);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

void StoreF32(uint8_t* base, uint32_t addr, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  REX_STORE_U32(addr, bits);
}

// VMX128 vmsum3fp as the recompiler does it (SSE4.1 dpps over x, y, z, w
// masked out), read from host lane 3 = guest element 0, like the original.
// The lane matters for NaNs: on an i9-10900K dpps broadcasts *different* NaN
// payloads to odd and even lanes when two inputs are NaN (x NaN in lanes 0/2,
// y NaN in lanes 1/3), so the original's NaN result is host-CPU specific.
float Dot3(float x, float y, float z, float w) {
  const simde__m128 v = simde_mm_set_ps(x, y, z, w);  // host lanes 3..0
  const simde__m128 sum = simde_mm_dp_ps(v, v, 0xEF);
  return simde_mm_cvtss_f32(simde_mm_shuffle_ps(sum, sum, 0xFF));
}

}  // namespace

extern "C" REX_FUNC(rewrite_NormalizeVector_82260BC8) {
  const uint32_t v = ctx.r3.u32 & ~0xFu;  // lvx/stvx ignore the low 4 bits
  float c[4];
  for (int i = 0; i < 4; ++i) c[i] = LoadF32(base, v + 4 * i);  // gate: equivalent: a 5th pass only overruns the local c[4] (UB, no guest state)

  ctx.fpscr.enableFlushMode();
  const float dot = Dot3(c[0], c[1], c[2], c[3]);
  ctx.fpscr.disableFlushModeUnconditional();
  const float len = float(std::sqrt(double(dot)));

  if (double(len) < ctx.f1.f64) {
    for (int i = 0; i < 4; ++i) StoreF32(base, v + 4 * i, 0.0f);
    ctx.fpscr.disableFlushMode();
    ctx.f1.f64 = 0.0;
    return;
  }
  ctx.f1.f64 = len;
  const float inv = float(1.0 / double(len));
  ctx.fpscr.enableFlushModeUnconditional();
  for (int i = 0; i < 4; ++i) StoreF32(base, v + 4 * i, c[i] * inv);
}

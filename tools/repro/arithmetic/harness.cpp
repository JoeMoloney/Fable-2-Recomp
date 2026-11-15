// Compare unchanged source under two ISA targets. This does not establish PPC
// hardware accuracy or whole-game compatibility. All FP exception traps masked.
#include "fable_2_pch.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#pragma STDC FENV_ACCESS ON
REX_EXTERN(arith_base_norm);
REX_EXTERN(arith_fma_norm);
REX_EXTERN(arith_base_geometry);
REX_EXTERN(arith_fma_geometry);
REX_EXTERN(arith_base_filter);
REX_EXTERN(arith_fma_filter);
extern "C" double arith_base_primitive(double, double, double);
extern "C" double arith_fma_primitive(double, double, double);

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

constexpr size_t kLow = 0x1000, kLowSize = 0x6000, kConstant = 0x82000000;
class Arena {
 public:
  Arena() {
    base = static_cast<uint8_t*>(mmap(nullptr, 0x100000000ull, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    require(base != MAP_FAILED, "sparse guest arena reservation failed");
    require(sysconf(_SC_PAGESIZE) == 4096, "fixture requires 4-KiB host pages");
    require(mprotect(base + kLow, kLowSize, PROT_READ | PROT_WRITE) == 0,
            "low guest pages commit failed");
    require(mprotect(base + kConstant, 4096, PROT_READ | PROT_WRITE) == 0,
            "constant guest page commit failed");
    // Exact eight bytes observed via Ghidra at 0x82000C08: float 1 then 0.
    const uint8_t constants[] = {0x3f, 0x80, 0, 0, 0, 0, 0, 0};
    std::memcpy(base + kConstant + 0xC08, constants, sizeof(constants));
  }
  ~Arena() { munmap(base, 0x100000000ull); }
  void clear() { std::memset(base + kLow, 0xA5, kLowSize); }
  uint8_t* base{};
};

void store32(uint8_t* memory, unsigned address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(memory + address, &value, 4);
}
void store_float(uint8_t* memory, unsigned address, float value) {
  store32(memory, address, std::bit_cast<uint32_t>(value));
}
void reset_fp(int rounding, unsigned flush) {
  std::fesetenv(FE_DFL_ENV);
  require(std::fesetround(rounding) == 0, "rounding mode not supported");
  auto csr = _mm_getcsr();
  csr &= ~0x803Fu; // Clear status and FTZ, retaining rounding/masks.
  csr &= ~0x40u;   // DAZ.
  if (flush) csr |= 0x8040;
  _mm_setcsr(csr);
  errno = 0;
}
struct FPResult {
  int exceptions;
  unsigned mxcsr;
  int error;
  bool operator==(const FPResult&) const = default;
};
FPResult capture_fp() {
  int error = errno;
  auto csr = _mm_getcsr();
  return {std::fetestexcept(FE_ALL_EXCEPT), csr, error};
}
PPCContext initial_context(int round, unsigned flush) {
  PPCContext ctx{};
  auto* p = reinterpret_cast<unsigned char*>(&ctx);
  for (size_t i = 0; i < sizeof(ctx); ++i) p[i] = (i * 19 + 31) & 255;
  reset_fp(round, flush);
  ctx.fpscr.csr = _mm_getcsr();
  ctx.xer.so = 0;
  ctx.xer.ov = 0;
  ctx.xer.ca = 0;
  return ctx;
}

size_t check_pair(PPCFunc* baseline, PPCFunc* targeted, PPCContext initial,
                  Arena& off, Arena& on, int round, unsigned flush,
                  const char* label, bool corrupt = false) {
  std::memcpy(on.base + kLow, off.base + kLow, kLowSize);
  auto a = initial, b = initial;
  reset_fp(round, flush);
  baseline(a, off.base);
  auto a_fp = capture_fp();
  reset_fp(round, flush);
  targeted(b, on.base);
  auto b_fp = capture_fp();
  if (corrupt) {
    b.f1.u64 ^= 1;
    require(std::memcmp(&a, &b, sizeof(a)) == 0, "deliberate output corruption detected");
  }
  if (std::memcmp(&a, &b, sizeof(a)) ||
      std::memcmp(off.base + kLow, on.base + kLow, kLowSize) || !(a_fp == b_fp)) {
    std::fprintf(stderr, "MISMATCH %s rounding=%d flush=%u f1=%016llx/%016llx "
                        "exceptions=%x/%x mxcsr=%x/%x errno=%d/%d\n", label, round, flush,
                  (unsigned long long)a.f1.u64, (unsigned long long)b.f1.u64,
                  a_fp.exceptions, b_fp.exceptions, a_fp.mxcsr, b_fp.mxcsr, a_fp.error, b_fp.error);
    const auto* aa = reinterpret_cast<const uint8_t*>(&a);
    const auto* bb = reinterpret_cast<const uint8_t*>(&b);
    unsigned shown = 0;
    for (size_t i = 0; i < sizeof(a) && shown < 16; ++i) {
      if (aa[i] != bb[i]) {
        std::fprintf(stderr, "context_byte_offset=%zu baseline=%02x targeted=%02x\n", i, aa[i], bb[i]);
        ++shown;
      }
    }
    std::array<const PPCRegister*, 3> diffs = {&a.f0, &a.f10, &a.f13};
    std::array<const PPCRegister*, 3> other = {&b.f0, &b.f10, &b.f13};
    for (unsigned i = 0; i < diffs.size(); ++i)
      std::fprintf(stderr, "diagnostic_fpr_%u=%016llx/%016llx\n", i,
                   (unsigned long long)diffs[i]->u64, (unsigned long long)other[i]->u64);
    std::array<const PPCRegister*, 9> inputs = {&initial.f1, &initial.f2, &initial.f3,
        &initial.f4, &initial.f5, &initial.f6, &initial.f7, &initial.f8, &initial.f9};
    for (unsigned i = 0; i < inputs.size(); ++i)
      std::fprintf(stderr, "input_f%u=%016llx\n", i + 1, (unsigned long long)inputs[i]->u64);
    throw std::runtime_error("strict whole-context/memory/fenv equivalence failed");
  }
  return 1;
}

uint64_t rng(uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}
constexpr std::array<uint64_t, 18> kDoubleEdges = {
  0, 0x8000000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
  0x3ff0000000000001ull, 0x3fefffffffffffffull, 1, 0x8000000000000001ull,
  0x0010000000000000ull, 0x000fffffffffffffull, 0x7fefffffffffffffull,
  0xffefffffffffffffull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
  0x7ff8000000000011ull, 0xfff8000000000022ull,
  0x7ff0000000000033ull, 0xfff0000000000044ull,
};
constexpr std::array<uint32_t, 16> kFloatEdges = {
  0, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x3f800001u, 0x3f7fffffu,
  1, 0x80000001u, 0x00800000u, 0x007fffffu, 0x7f7fffffu, 0xff7fffffu,
  0x7f800000u, 0xff800000u, 0x7fc00011u, 0xffc00022u,
};

size_t primitive_tests(int round, unsigned flush) {
  size_t count = 0;
  auto one = [&](uint64_t aa, uint64_t bb, uint64_t cc) {
    double a = std::bit_cast<double>(aa), b = std::bit_cast<double>(bb), c = std::bit_cast<double>(cc);
    reset_fp(round, flush);
    uint64_t off = std::bit_cast<uint64_t>(arith_base_primitive(a, b, c));
    auto off_fp = capture_fp();
    reset_fp(round, flush);
    uint64_t on = std::bit_cast<uint64_t>(arith_fma_primitive(a, b, c));
    auto on_fp = capture_fp();
    if (off != on || !(off_fp == on_fp)) {
      std::fprintf(stderr, "MISMATCH primitive a=%016llx b=%016llx c=%016llx "
                          "result=%016llx/%016llx rounding=%d flush=%u "
                          "exceptions=%x/%x mxcsr=%x/%x errno=%d/%d\n",
                    (unsigned long long)aa, (unsigned long long)bb, (unsigned long long)cc,
                    (unsigned long long)off, (unsigned long long)on, round, flush,
                    off_fp.exceptions, on_fp.exceptions, off_fp.mxcsr, on_fp.mxcsr,
                    off_fp.error, on_fp.error);
      throw std::runtime_error("strict primitive equivalence failed");
    }
    ++count;
  };
  for (auto a : kDoubleEdges) for (auto b : kDoubleEdges) for (auto c : kDoubleEdges) one(a, b, c);
  uint64_t state = 0x8f7b1acde0927345ull;
  for (unsigned i = 0; i < 10000; ++i) {
    uint64_t a = rng(state), b = rng(state), c = rng(state);
    one(a, b, c);
  }
  return count;
}

void initialize_filter(Arena& memory, PPCContext& ctx, unsigned n, unsigned position,
                       unsigned adjusting) {
  memory.clear();
  ctx.r3.u64 = 0x1000;
  ctx.f1.f64 = 0.25;
  store32(memory.base, 0x1008, adjusting);
  store32(memory.base, 0x100C, 0x2000);
  store32(memory.base, 0x1010, 0x3000);
  store32(memory.base, 0x1014, 0x4000);
  store32(memory.base, 0x1024, n);
  store32(memory.base, 0x1028, position);
  for (unsigned i = 0; i <= n; ++i) {
    store_float(memory.base, 0x2000 + i * 4, float(int(i % 13) - 6) * 0.03125f);
    store_float(memory.base, 0x3000 + i * 4, float(int(i % 7) - 3) * 0.00390625f);
    store_float(memory.base, 0x4000 + i * 4, float(int(i % 5) - 2) * 0.0000152587890625f);
  }
}

void benchmark(Arena& a, Arena& b) {
  for (unsigned sample = 0; sample < 4; ++sample) {
    PPCFunc* off = sample == 0 ? arith_base_norm : sample == 1 ? arith_base_geometry : arith_base_filter;
    PPCFunc* on = sample == 0 ? arith_fma_norm : sample == 1 ? arith_fma_geometry : arith_fma_filter;
    auto initial = initial_context(FE_TONEAREST, 0);
    a.clear();
    if (sample == 0) {
      initial.r3.u64 = 0x1000;
      store_float(a.base, 0x1000, 1.125f); store_float(a.base, 0x1004, -2.25f);
      store_float(a.base, 0x1008, 3.5f);
    } else if (sample == 1) {
      double values[] = {0, 0, 1, 1, 0, 1, 1, 0, 0.00001};
      std::memcpy(&initial.f1, values, sizeof(values));
    } else initialize_filter(a, initial, sample == 2 ? 16 : 128, 0, 0);
    std::memcpy(b.base + kLow, a.base + kLow, kLowSize);
    std::array<std::vector<double>, 2> times;
    for (unsigned round = 0; round < 9; ++round) {
      for (unsigned order = 0; order < 2; ++order) {
        unsigned variant = order ^ (round & 1);
        auto ctx = initial;
        uint8_t* base = variant ? b.base : a.base;
        PPCFunc* fn = variant ? on : off;
        reset_fp(FE_TONEAREST, 0);
        constexpr unsigned iterations = 20000;
        auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < iterations; ++i) {
          if (sample == 1) std::memcpy(&ctx.f1, &initial.f1, 9 * sizeof(PPCRegister));
          if (sample >= 2) ctx.f1.f64 = 0.25;
          fn(ctx, base);
        }
        double ns = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count() / iterations;
        if (round) times[variant].push_back(ns);
      }
    }
    for (auto& t : times) std::sort(t.begin(), t.end());
    std::printf("BENCH sample=%u base_ns=%.3f fma_ns=%.3f base_min=%.3f base_max=%.3f "
                "fma_min=%.3f fma_max=%.3f\n", sample,
                (times[0][3] + times[0][4]) / 2, (times[1][3] + times[1][4]) / 2,
                times[0].front(), times[0].back(), times[1].front(), times[1].back());
  }
}

int main(int argc, char** argv) {
  try {
    require(__builtin_cpu_supports("avx") && __builtin_cpu_supports("fma"),
            "AVX/FMA not available; targeted code must not execute");
    std::string_view mode = argc > 1 ? argv[1] : "test";
    Arena off, on;
    if (mode == "bench") { benchmark(off, on); return 0; }
    if (mode == "negative-result") {
      auto ctx = initial_context(FE_TONEAREST, 0);
      off.clear(); ctx.r3.u64 = 0x1000;
      store_float(off.base, 0x1000, 1); store_float(off.base, 0x1004, 2); store_float(off.base, 0x1008, 3);
      check_pair(arith_base_norm, arith_fma_norm, ctx, off, on, FE_TONEAREST, 0, "negative", true);
      throw std::runtime_error("negative result control failed");
    }
    if (mode == "negative-unfused") {
      reset_fp(FE_TONEAREST, 0);
      volatile double a = 1.0 + 0x1p-27, b = 1.0 - 0x1p-27;
      volatile double product = a * b;
      double unfused = product - 1.0;
      double fused = arith_base_primitive(a, b, -1.0);
      require(fused == -0x1p-54 && unfused == 0, "negative fused oracle invalid");
      require(std::bit_cast<uint64_t>(fused) == std::bit_cast<uint64_t>(unfused),
              "unfused arithmetic rejected");
      throw std::runtime_error("negative unfused control failed");
    }
    size_t primitive_count = 0, norm_count = 0, geometry_count = 0, filter_count = 0;
    for (int round : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
      for (unsigned flush : {0u, 1u}) {
        primitive_count += primitive_tests(round, flush);
        for (auto x : kFloatEdges) for (auto y : kFloatEdges) for (auto z : kFloatEdges) {
          off.clear();
          auto ctx = initial_context(round, flush); ctx.r3.u64 = 0x1000;
          store32(off.base, 0x1000, x); store32(off.base, 0x1004, y); store32(off.base, 0x1008, z);
          norm_count += check_pair(arith_base_norm, arith_fma_norm, ctx, off, on, round, flush, "norm");
        }
        uint64_t state = 0x1ab8ee;
        for (unsigned i = 0; i < 1000; ++i) {
          off.clear(); auto ctx = initial_context(round, flush);
          std::array<PPCRegister*, 9> registers = {
              &ctx.f1, &ctx.f2, &ctx.f3, &ctx.f4, &ctx.f5, &ctx.f6, &ctx.f7, &ctx.f8, &ctx.f9};
          for (unsigned j = 0; j < 9; ++j) {
            uint32_t bits = i < kFloatEdges.size() ? kFloatEdges[(i + j) % kFloatEdges.size()] :
                            uint32_t(rng(state));
            // Input registers model binary32 values promoted into guest FPRs.
            registers[j]->f64 = double(std::bit_cast<float>(bits));
          }
          geometry_count += check_pair(arith_base_geometry, arith_fma_geometry, ctx,
                                       off, on, round, flush, "geometry");
        }
        for (unsigned n : {1u, 2u, 3u, 8u, 16u, 64u, 128u, 256u}) {
          for (unsigned position : {0u, n / 2, n - 1}) for (unsigned adjusting : {0u, 1u, 4u}) {
            auto ctx = initial_context(round, flush);
            initialize_filter(off, ctx, n, position, adjusting);
            filter_count += check_pair(arith_base_filter, arith_fma_filter, ctx,
                                       off, on, round, flush, "filter");
          }
        }
      }
    }
    std::printf("PASS arithmetic primitive=%zu norm=%zu geometry=%zu filter=%zu "
                "rounding_modes=4 initial_flush_modes=2 full_context_memory_fenv_errno=exact\n",
                primitive_count, norm_count, geometry_count, filter_count);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}

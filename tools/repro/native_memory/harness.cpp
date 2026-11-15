// RAM-kernel comparison only. Does NOT assert full guest-context equivalence.
#include "fable_2_pch.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

REX_EXTERN(copy_translated);
REX_EXTERN(copy_native);
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
PPCContext seed_context(unsigned seed) {
  PPCContext ctx{};
  auto* bytes = reinterpret_cast<unsigned char*>(&ctx);
  for (size_t i = 0; i < sizeof(ctx); ++i) bytes[i] = (i * 37 + seed * 53) & 255;
  ctx.xer.so = seed & 1;
  ctx.xer.ov = (seed >> 1) & 1;
  ctx.xer.ca = (seed >> 2) & 1;
  return ctx;
}
void check_translated_preservation(PPCContext result, const PPCContext& seed) {
  // Exact clobber set in the audited 0x82CA30E8 body. Everything else, including
  // r3, nonvolatiles, LR, XER, FPSCR and all FPR/VMX registers, must stay unchanged.
  result.r0 = seed.r0; result.r4 = seed.r4; result.r5 = seed.r5;
  result.r6 = seed.r6; result.r7 = seed.r7; result.r8 = seed.r8;
  result.r9 = seed.r9; result.r10 = seed.r10;
  result.ctr = seed.ctr; result.cr0 = seed.cr0;
  require(std::memcmp(&result, &seed, sizeof(seed)) == 0,
          "translated unexpectedly changed preserved context");
}
void one(uint8_t* original_memory, uint8_t* native_memory, size_t size,
         unsigned dst, unsigned src, unsigned length, unsigned seed,
         bool bad_oracle = false, bool bad_context = false, bool omit_native_copy = false) {
  require(uint64_t(src) + length <= size && uint64_t(dst) + length <= size,
          "invalid fixture range");
  require(length == 0 || src + length <= dst || dst + length <= src,
          "fixture accidentally overlaps");
  std::vector<uint8_t> expected(size);
  for (size_t i = 0; i < size; ++i)
    expected[i] = (i * 29 + (i >> 8) * 73 + (i >> 13) * 19 + seed * 17) & 255;
  std::memcpy(original_memory, expected.data(), size);
  std::memcpy(native_memory, expected.data(), size);
  // Independent byte oracle, not the native kernel's memcpy implementation.
  for (unsigned i = 0; i < length; ++i) expected[dst + i] = expected[src + i];
  if (bad_oracle) expected[dst] ^= 1;
  auto input = seed_context(seed);
  input.r3.u64 = dst; input.r4.u64 = src; input.r5.u64 = length;
  auto original = input, native = input;
  copy_translated(original, original_memory);
  if (!omit_native_copy) copy_native(native, native_memory);
  require(std::memcmp(original_memory, expected.data(), size) == 0,
          "translated differs from independent byte oracle");
  require(std::memcmp(native_memory, expected.data(), size) == 0,
          "native differs from independent byte oracle");
  check_translated_preservation(original, input);
  if (bad_context) native.r31.u64 ^= 1;
  require(std::memcmp(&native, &input, sizeof(input)) == 0,
          "native unexpectedly changed context");
}
class GuardedPage {
 public:
  GuardedPage() {
    size = sysconf(_SC_PAGESIZE);
    require(size >= 4096, "unexpected page size");
    allocation = mmap(nullptr, size * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(allocation != MAP_FAILED, "guard reservation failed");
    data = static_cast<uint8_t*>(allocation) + size;
    require(mprotect(data, size, PROT_READ | PROT_WRITE) == 0, "guard commit failed");
  }
  ~GuardedPage() { munmap(allocation, size * 3); }
  void* allocation{};
  uint8_t* data{};
  size_t size{};
};
void benchmark() {
  alignas(64) std::array<uint8_t, 32768> memory{};
  for (unsigned length : {64u, 256u, 4096u}) for (unsigned alignment : {0u, 1u}) {
    std::array<std::vector<double>, 2> times;
    for (unsigned round = 0; round < 9; ++round) for (unsigned order = 0; order < 2; ++order) {
      unsigned variant = order ^ (round & 1);
      PPCFunc* fn = variant ? copy_native : copy_translated;
      auto ctx = seed_context(0);
      constexpr unsigned iterations = 20000;
      auto start = std::chrono::steady_clock::now();
      for (unsigned i = 0; i < iterations; ++i) {
        ctx.r3.u64 = 16384; ctx.r4.u64 = 4096 + alignment; ctx.r5.u64 = length;
        fn(ctx, memory.data());
      }
      double ns = std::chrono::duration<double, std::nano>(
          std::chrono::steady_clock::now() - start).count() / iterations;
      if (round) times[variant].push_back(ns);
    }
    for (auto& t : times) std::sort(t.begin(), t.end());
    std::printf("BENCH length=%u source_alignment=%u translated_ns=%.3f native_ns=%.3f "
                "translated_min=%.3f translated_max=%.3f native_min=%.3f native_max=%.3f\n",
                length, alignment, (times[0][3] + times[0][4]) / 2,
                (times[1][3] + times[1][4]) / 2, times[0].front(), times[0].back(),
                times[1].front(), times[1].back());
  }
}
int main(int argc, char** argv) {
  try {
    std::string_view mode = argc > 1 ? argv[1] : "test";
    if (mode == "bench") { benchmark(); return 0; }
    alignas(64) std::array<uint8_t, 32768> original{}, native{};
    if (mode == "negative-oracle" || mode == "negative-context" || mode == "negative-noop") {
      one(original.data(), native.data(), original.size(), 16384, 4096, 16, 7,
          mode == "negative-oracle", mode == "negative-context", mode == "negative-noop");
      throw std::runtime_error("negative control failed to detect corruption");
    }
    std::vector<unsigned> lengths;
    for (unsigned i = 0; i <= 65; ++i) lengths.push_back(i);
    for (unsigned n : {127, 128, 129, 255, 256, 257, 511, 512, 513,
                       1023, 1024, 1025, 4095, 4096, 4097}) lengths.push_back(n);
    size_t cases = 0, guarded = 0;
    for (unsigned seed : {0u, 7u}) for (unsigned src = 0; src < 16; ++src)
      for (unsigned dst = 0; dst < 16; ++dst) for (unsigned n : lengths) {
        one(original.data(), native.data(), original.size(), 16384 + dst, 4096 + src, n, seed);
        ++cases;
      }
    GuardedPage a, b;
    for (unsigned n : lengths) if (n <= 1025) for (bool source_at_end : {false, true}) {
      unsigned src = source_at_end ? a.size - n : 0;
      unsigned dst = source_at_end ? 0 : a.size - n;
      one(a.data, b.data, a.size, dst, src, n, 7); ++guarded;
    }
    std::printf("PASS native_memory cases=%zu guarded=%zu independent_byte_oracle=exact "
                "preserved_context=exact full_context_equivalence=NOT_CLAIMED\n", cases, guarded);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
  }
}

// Offline tests of actual emitted leaf routines, not a game/runtime harness.
#include "fable_2_pch.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

REX_EXTERN(probe_off_getter);
REX_EXTERN(probe_on_getter);
REX_EXTERN(probe_off_copy);
REX_EXTERN(probe_on_copy);

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

PPCContext seed_context(unsigned seed) {
  static_assert(std::is_trivially_copyable_v<PPCContext>);
  PPCContext ctx{};
  auto* bytes = reinterpret_cast<unsigned char*>(&ctx);
  for (size_t i = 0; i < sizeof(ctx); ++i) bytes[i] = (i * 37 + seed * 53) & 255;
  ctx.xer.so = seed & 1;
  ctx.xer.ov = (seed >> 1) & 1;
  ctx.xer.ca = (seed >> 2) & 1;
  return ctx;
}

void same_except(PPCContext off, PPCContext on, bool getter) {
  // Deliberately narrow allowance: only the one localized register touched by
  // this leaf may differ. CTR/CR/XER and every other context byte must agree.
  if (getter) off.r11 = on.r11;
  else off.r0 = on.r0;
  require(std::memcmp(&off, &on, sizeof(off)) == 0, "unexpected context difference");
}

void getter_test(bool require_scratch) {
  alignas(64) std::array<uint8_t, 64> memory{};
  for (unsigned seed = 0; seed < 8; ++seed) {
    auto initial = seed_context(seed);
    auto off = initial, on = initial;
    probe_off_getter(off, memory.data());
    probe_on_getter(on, memory.data());
    require(off.r3.u64 == 0xffffffff832eecf0ull && on.r3.u64 == off.r3.u64,
            "getter result differs from independent constant oracle");
    require(on.r11.u64 == initial.r11.u64, "localized getter published r11");
    require(off.r11.u64 == 0xffffffff832f0000ull, "baseline getter r11 mismatch");
    same_except(off, on, true);
    // Negative control: a handwritten consumer that requires r11 live across
    // this call is NOT compatible with localization. This MUST fail when asked.
    if (require_scratch)
      require(off.r11.u64 == on.r11.u64, "scratch-live-across-call contract mismatch");
  }
}

void copy_case(uint8_t* off_memory, uint8_t* on_memory, size_t arena_size,
               uint32_t dst, uint32_t src, uint32_t length, unsigned seed,
               bool corrupt_oracle = false) {
  std::vector<uint8_t> expected(arena_size);
  for (size_t i = 0; i < arena_size; ++i) expected[i] = (i * 29 + seed * 17) & 255;
  std::memcpy(off_memory, expected.data(), arena_size);
  std::memcpy(on_memory, expected.data(), arena_size);
  // Test contract: valid disjoint RAM ranges, no address wrap, no device memory.
  require(uint64_t(src) + length <= arena_size && uint64_t(dst) + length <= arena_size,
          "invalid fixture range");
  require(length == 0 || dst + length <= src || src + length <= dst,
          "fixture accidentally overlaps");
  std::memcpy(expected.data() + dst, expected.data() + src, length);
  if (corrupt_oracle) expected[dst] ^= 1;
  auto initial = seed_context(seed);
  initial.r3.u64 = dst;
  initial.r4.u64 = src;
  initial.r5.u64 = length;
  auto off = initial, on = initial;
  probe_off_copy(off, off_memory);
  probe_on_copy(on, on_memory);
  require(std::memcmp(off_memory, expected.data(), arena_size) == 0,
          "baseline memory differs from independent copy oracle");
  require(std::memcmp(on_memory, expected.data(), arena_size) == 0,
          "localized memory differs from independent copy oracle");
  require(on.r0.u64 == initial.r0.u64, "localized copy published r0");
  require(off.r3.u64 == dst && on.r3.u64 == dst, "copy changed r3");
  same_except(off, on, false);
}

class GuardedPage {
 public:
  GuardedPage() {
    page_ = sysconf(_SC_PAGESIZE);
    require(page_ >= 4096, "unexpected host page size");
    allocation_ = mmap(nullptr, page_ * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(allocation_ != MAP_FAILED, "guard mmap failed");
    data_ = static_cast<uint8_t*>(allocation_) + page_;
    require(mprotect(data_, page_, PROT_READ | PROT_WRITE) == 0, "guard mprotect failed");
  }
  ~GuardedPage() { munmap(allocation_, page_ * 3); }
  uint8_t* data() const { return data_; }
  uint32_t size() const { return page_; }
 private:
  void* allocation_{};
  uint8_t* data_{};
  size_t page_{};
};

void benchmark() {
  alignas(64) std::array<uint8_t, 32768> memory{};
  for (unsigned length : {64u, 256u, 4096u}) {
    for (unsigned alignment : {0u, 1u}) {
      std::array<std::vector<double>, 2> times;
      for (unsigned round = 0; round < 9; ++round) {
        for (unsigned order = 0; order < 2; ++order) {
          unsigned variant = order ^ (round & 1);
          PPCFunc* fn = variant ? probe_on_copy : probe_off_copy;
          auto ctx = seed_context(0);
          constexpr unsigned iterations = 20000;
          auto start = std::chrono::steady_clock::now();
          for (unsigned i = 0; i < iterations; ++i) {
            ctx.r3.u64 = 16384;
            ctx.r4.u64 = 4096 + alignment;
            ctx.r5.u64 = length;
            fn(ctx, memory.data());
          }
          auto elapsed = std::chrono::duration<double, std::nano>(
              std::chrono::steady_clock::now() - start).count() / iterations;
          if (round) times[variant].push_back(elapsed); // first round is warmup
        }
      }
      for (auto& t : times) std::sort(t.begin(), t.end());
      std::printf("BENCH length=%u source_alignment=%u off_median_ns=%.3f on_median_ns=%.3f "
                  "off_min_ns=%.3f off_max_ns=%.3f on_min_ns=%.3f on_max_ns=%.3f\n",
                  length, alignment, (times[0][3] + times[0][4]) / 2,
                  (times[1][3] + times[1][4]) / 2, times[0].front(), times[0].back(),
                  times[1].front(), times[1].back());
    }
  }
}

int main(int argc, char** argv) {
  try {
    std::string_view mode = argc > 1 ? argv[1] : "test";
    if (mode == "bench") { benchmark(); return 0; }
    getter_test(mode == "negative-scratch");
    alignas(64) std::array<uint8_t, 32768> off_memory{}, on_memory{};
    if (mode == "negative-memory") {
      copy_case(off_memory.data(), on_memory.data(), off_memory.size(),
                16384, 4096, 16, 0, true);
      throw std::runtime_error("negative memory control failed to detect corruption");
    }
    std::vector<unsigned> lengths;
    for (unsigned i = 0; i <= 65; ++i) lengths.push_back(i);
    for (unsigned n : {127, 128, 129, 255, 256, 257, 511, 512, 513,
                       1023, 1024, 1025, 4095, 4096, 4097}) lengths.push_back(n);
    size_t count = 0;
    for (unsigned seed : {0u, 7u})
      for (unsigned src_align = 0; src_align < 16; ++src_align)
        for (unsigned dst_align = 0; dst_align < 16; ++dst_align)
          for (unsigned n : lengths) {
            copy_case(off_memory.data(), on_memory.data(), off_memory.size(),
                      16384 + dst_align, 4096 + src_align, n, seed);
            ++count;
          }
    GuardedPage off_page, on_page;
    size_t guarded_count = 0;
    for (unsigned n : lengths) {
      if (n > 1025) continue;
      for (bool source_at_end : {false, true}) {
        unsigned src = source_at_end ? off_page.size() - n : 0;
        unsigned dst = source_at_end ? 0 : off_page.size() - n;
        copy_case(off_page.data(), on_page.data(), off_page.size(), dst, src, n, 7);
        ++guarded_count;
      }
    }
    std::printf("PASS getter_cases=8 copy_cases=%zu guarded_cases=%zu "
                "context_comparison=all_bytes_except_r11_getter_or_r0_copy\n",
                count, guarded_count);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
  }
}

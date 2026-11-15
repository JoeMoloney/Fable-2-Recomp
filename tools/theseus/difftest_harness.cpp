// Differential test: original recompiled leaf function vs readable rewrite.
//
// Built by tools/theseus/difftest.py with -DORIGINAL=<sym> -DREWRITE=<sym>.
// Both functions run on identical random CPU states and guest memory, and the
// results are compared at two levels:
//   strict - every byte of the CPU context and the compared memory is equal
//   abi    - only what a caller may rely on under the PowerPC calling
//            convention: r3 (32-bit return), r1/r2, non-volatile r13-r31 and
//            f14-f31, cr2-cr4, lr, and memory. Scratch registers may differ.
// The upper 32 bits of r3 are reported separately (r3_high), because whether a
// caller ever reads them is a property of the callers (tools/theseus/caller_usage.py).
//
// Guest memory is a 4 GB lazily-allocated mapping, so any 32-bit address a
// function touches is valid; pointers in argument registers point into a
// 64 KB "data window" of random bytes (with frequent zero bytes so strings end).
// The data window and stack are compared after every case. Every page either
// function ever touched (mincore) is compared every kScanEvery cases and at the
// end: memory outside the windows is never reset, so a stray write that differs
// stays visible until the next scan.
//
// Input rules (optional, tools/theseus/rewrites/<name>.rules, turned into
// difftest_rules.inc by difftest.py) fix what an argument register holds:
//   r3 ptr        pointer into the data window (at least 32 KB room after it)
//   r5 len 256    0..256
//   r4 int        any sign-extended 32-bit value
//   r6 ptr_or_null, r7 any (64-bit), r8 u32
//   r4 near r3 32 half the time r3 + 0..32 (e.g. a needle inside the haystack),
//                 otherwise a normal pointer (rules apply in file order)
// Registers without a rule get the default random mix.
//   mem32 0x832EF350 ptr   sets a 32-bit global (big-endian) before each case,
//                          e.g. a table pointer, so the function sees real data
//                          instead of the harness's all-zero memory.
#include "fable_2_pch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <vector>

extern "C" REX_FUNC(ORIGINAL);
extern "C" REX_FUNC(REWRITE);

namespace {

constexpr uint64_t kGuestSize = 0x100000000ull + 0x10000;
constexpr uint32_t kWindow = 0x40000000, kWindowSize = 0x10000;
constexpr uint32_t kStackTop = 0x50008000, kStackSize = 0x8000;

struct Rng {
  uint64_t s;
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  uint32_t below(uint32_t n) { return uint32_t(next() % n); }
};

uint8_t* map_guest() {
  void* p = mmap(nullptr, kGuestSize, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) { std::perror("mmap"); std::exit(2); }
  return static_cast<uint8_t*>(p);
}

enum Kind { kPtr, kPtrOrNull, kLen, kInt, kU32, kAny, kNear };
struct ArgRule { int reg; Kind kind; uint32_t max; int base = 0; };
struct MemRule { uint32_t addr; Kind kind; uint32_t max; };
#ifdef HAS_RULES
#include "difftest_rules.inc"
#else
const ArgRule kRules[] = {{0, kAny, 0}};  // r0: never an argument, so unused
const MemRule kMemRules[] = {{0, kAny, 0}};  // address 0: unused
#endif

uint64_t ruled_value(Rng& r, const ArgRule& rule) {
  switch (rule.kind) {
    case kPtr: return kWindow + r.below(kWindowSize / 2);
    case kPtrOrNull: return r.below(8) == 0 ? 0 : kWindow + r.below(kWindowSize / 2);
    case kLen: return r.below(rule.max + 1);
    case kInt: return uint64_t(int64_t(int32_t(r.next())));
    case kU32: return uint32_t(r.next());
    default: return r.next();
  }
}

// Number of resident pages over the 4 GB guest map: which pages were touched.
constexpr size_t kPage = 4096;
std::vector<unsigned char> resident(uint8_t* mem) {
  std::vector<unsigned char> v(kGuestSize / kPage);
  if (mincore(mem, kGuestSize, v.data()) != 0) { std::perror("mincore"); std::exit(2); }
  return v;
}

// Compares every page touched in either map; returns differing pages and
// copies a's version into b so later cases start from equal memory again.
long scan_touched(uint8_t* a, uint8_t* b, uint32_t* first_page) {
  auto ra = resident(a), rb = resident(b);
  long bad = 0;
  for (size_t i = 0; i < ra.size(); ++i) {
    if (!((ra[i] | rb[i]) & 1)) continue;
    if (std::memcmp(a + i * kPage, b + i * kPage, kPage) != 0) {
      if (!bad && first_page) *first_page = uint32_t(i * kPage);
      ++bad;
      std::memcpy(b + i * kPage, a + i * kPage, kPage);
    }
  }
  return bad;
}

uint64_t arg_value(Rng& r) {
  switch (r.below(6)) {
    case 0: case 1: return kWindow + r.below(kWindowSize - 64);   // pointer into data
    case 2: return r.below(64);                                   // small int / length
    case 3: return uint32_t(r.next());                            // any 32-bit value
    case 4: return uint64_t(int64_t(int32_t(r.next())));          // sign-extended 32-bit
    default: return r.next();                                     // any 64-bit value
  }
}

double fp_value(Rng& r) {
  static const double nice[] = {0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 1e-30, 1e30, 3.14159};
  if (r.below(2)) return nice[r.below(sizeof(nice) / sizeof(nice[0]))];
  return (double(int64_t(r.next() >> 11)) / double(1ull << 40)) - 2048.0;
}

struct Field { const char* name; size_t off, size; bool abi; };
#define F(m, abi) Field{#m, offsetof(PPCContext, m), sizeof(PPCContext::m), abi}
const Field kFields[] = {
  F(r0, 0), F(r1, 1), F(r2, 1), F(r4, 0), F(r5, 0), F(r6, 0), F(r7, 0), F(r8, 0),
  F(r9, 0), F(r10, 0), F(r11, 0), F(r12, 0), F(r13, 1), F(r14, 1), F(r15, 1),
  F(r16, 1), F(r17, 1), F(r18, 1), F(r19, 1), F(r20, 1), F(r21, 1), F(r22, 1),
  F(r23, 1), F(r24, 1), F(r25, 1), F(r26, 1), F(r27, 1), F(r28, 1), F(r29, 1),
  F(r30, 1), F(r31, 1), F(lr, 1), F(ctr, 0), F(xer, 0), F(reserved, 0),
  F(cr0, 0), F(cr1, 0), F(cr2, 1), F(cr3, 1), F(cr4, 1), F(cr5, 0), F(cr6, 0), F(cr7, 0),
  F(fpscr, 0), F(f0, 0), F(f1, 0), F(f2, 0), F(f3, 0), F(f4, 0), F(f5, 0), F(f6, 0),
  F(f7, 0), F(f8, 0), F(f9, 0), F(f10, 0), F(f11, 0), F(f12, 0), F(f13, 0),
  F(f14, 1), F(f15, 1), F(f16, 1), F(f17, 1), F(f18, 1), F(f19, 1), F(f20, 1),
  F(f21, 1), F(f22, 1), F(f23, 1), F(f24, 1), F(f25, 1), F(f26, 1), F(f27, 1),
  F(f28, 1), F(f29, 1), F(f30, 1), F(f31, 1),
};
#undef F

}  // namespace

int main(int argc, char** argv) {
  const long cases = argc > 1 ? std::atol(argv[1]) : 200000;
  const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0x9E3779B97F4A7C15ull;
  uint8_t* mem_a = map_guest();
  uint8_t* mem_b = map_guest();
  Rng rng{seed};
  std::vector<uint8_t> window(kWindowSize), stack(kStackSize);

  long strict_bad = 0, abi_bad = 0, r3_low_bad = 0, r3_high_bad = 0, mem_bad = 0;
  long field_bad[sizeof(kFields) / sizeof(kFields[0])] = {};
  long first_abi_case = -1;
  constexpr long kScanEvery = 1000;
  long stray_pages = 0, first_stray_scan = -1;
  uint32_t first_stray_page = 0;

  for (long c = 0; c < cases; ++c) {
    for (auto& b : window) b = rng.below(12) == 0 ? 0 : uint8_t(rng.next());
    for (auto& b : stack) b = uint8_t(rng.next());
    std::memcpy(mem_a + kWindow, window.data(), kWindowSize);
    std::memcpy(mem_b + kWindow, window.data(), kWindowSize);
    std::memcpy(mem_a + kStackTop - kStackSize, stack.data(), kStackSize);
    std::memcpy(mem_b + kStackTop - kStackSize, stack.data(), kStackSize);

    for (const MemRule& m : kMemRules) {
      if (!m.addr) continue;
      const uint32_t v = uint32_t(ruled_value(rng, {0, m.kind, m.max}));
      const uint8_t be[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
      std::memcpy(mem_a + m.addr, be, 4);
      std::memcpy(mem_b + m.addr, be, 4);
    }

    PPCContext init{};
    auto* bytes = reinterpret_cast<uint8_t*>(&init);
    for (size_t i = 0; i < sizeof(init); ++i) bytes[i] = uint8_t(rng.next());
    for (PPCRegister* r : {&init.r3, &init.r4, &init.r5, &init.r6, &init.r7,
                           &init.r8, &init.r9, &init.r10})
      r->u64 = arg_value(rng);
    PPCRegister* args[] = {&init.r3, &init.r4, &init.r5, &init.r6,
                           &init.r7, &init.r8, &init.r9, &init.r10};
    for (const ArgRule& rule : kRules)
      if (rule.reg >= 3 && rule.reg <= 10)
        args[rule.reg - 3]->u64 = rule.kind == kNear && rng.below(2)
            ? uint64_t(args[rule.base - 3]->u32 + rng.below(rule.max + 1))
            : ruled_value(rng, rule);
    for (PPCRegister* f : {&init.f1, &init.f2, &init.f3, &init.f4})
      f->f64 = fp_value(rng);
    init.r1.u64 = kStackTop - 0x100;
    init.xer.so = rng.below(2); init.xer.ov = rng.below(2); init.xer.ca = rng.below(2);

    PPCContext a = init, b = init;
    ORIGINAL(a, mem_a);
    REWRITE(b, mem_b);

    bool memory_same =
        std::memcmp(mem_a + kWindow, mem_b + kWindow, kWindowSize) == 0 &&
        std::memcmp(mem_a + kStackTop - kStackSize, mem_b + kStackTop - kStackSize, kStackSize) == 0;
    bool low_same = a.r3.u32 == b.r3.u32;
    bool high_same = (a.r3.u64 >> 32) == (b.r3.u64 >> 32);
    bool abi_fields_same = true;
    for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i) {
      const Field& f = kFields[i];
      if (std::memcmp(reinterpret_cast<uint8_t*>(&a) + f.off,
                      reinterpret_cast<uint8_t*>(&b) + f.off, f.size) != 0) {
        ++field_bad[i];
        if (f.abi) abi_fields_same = false;
      }
    }
    bool abi_ok = memory_same && low_same && abi_fields_same;
    bool strict_ok = abi_ok && high_same && std::memcmp(&a, &b, sizeof(a)) == 0;
    strict_bad += !strict_ok;
    abi_bad += !abi_ok;
    r3_low_bad += !low_same;
    r3_high_bad += !high_same;
    mem_bad += !memory_same;
    if (!abi_ok && first_abi_case < 0) first_abi_case = c;
    if ((c + 1) % kScanEvery == 0 || c + 1 == cases) {
      long n = scan_touched(mem_a, mem_b, first_stray_scan < 0 ? &first_stray_page : nullptr);
      if (n && first_stray_scan < 0) first_stray_scan = c;
      stray_pages += n;
    }
  }

  std::printf("cases=%ld seed=0x%llx\n", cases, (unsigned long long)seed);
  std::printf("abi_mismatches=%ld strict_mismatches=%ld\n", abi_bad, strict_bad);
  std::printf("r3_low32_mismatches=%ld r3_high32_mismatches=%ld memory_mismatches=%ld\n",
              r3_low_bad, r3_high_bad, mem_bad);
  std::printf("differing_fields:");
  for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i)
    if (field_bad[i]) std::printf(" %s%s=%ld", kFields[i].name, kFields[i].abi ? "(ABI!)" : "", field_bad[i]);
  std::printf("\n");
  if (first_abi_case >= 0) std::printf("first_abi_failure_case=%ld\n", first_abi_case);
  std::printf("touched_page_mismatches=%ld", stray_pages);
  if (first_stray_scan >= 0)
    std::printf(" first_at_page=0x%08x by_case<=%ld", first_stray_page, first_stray_scan);
  std::printf(" (all touched pages compared every %ld cases)\n", kScanEvery);
  std::printf("rules=%s\n", sizeof(kRules) / sizeof(kRules[0]) > 1 || kRules[0].reg || kMemRules[0].addr ? "yes" : "none");
  const bool fail = abi_bad || stray_pages;
  std::printf("RESULT %s\n", fail ? "FAIL" : (strict_bad ? "PASS-ABI" : "PASS-STRICT"));
  return fail ? 1 : 0;
}

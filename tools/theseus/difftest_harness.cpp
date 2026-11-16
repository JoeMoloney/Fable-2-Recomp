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
//   mem32 @r3+12 ptr       the same for a field of an argument's struct (set
//                          after the registers)
//   mem32 *0x834BDBC4+16 slen 300   a field of the struct a global points to
//                          (set the global with an earlier mem32 line)
//   r4 slen 64             -64..64
//   r3 fixed 0x4000C000    always this value (registers or mem32), e.g. to put a
//                          struct at a known address in the window's last 16 KB
//                          (ptr/len rules only use the first 32 KB)
//   tree 17 12 [n]         build a valid MSVC std::map/_Tree in the last 16 KB of
//                          the data window each case: head sentinel + 0..n nodes
//                          (default 24), node = {left +0, parent +4, right +8},
//                          u32 key at +12 (second number), isnil byte at +17
//                          (first number; head 1, nodes 0); a random BST shape
//                          over sorted distinct keys; head->left/parent/right =
//                          min/root/max. Then these kinds (registers or mem32):
//     tree_cont            pointer to a container {+0 nonzero proxy, +4 head, +8 size}
//     tree_node            a real node (1 in 16: the head, i.e. end())
//     tree_key             pointer to a u32 key: half an existing key, a quarter
//                          a key +-1, a quarter random
//   data 0x82000CB0 3FF0000000000000   fixed big-endian bytes before each case,
//                          e.g. a float constant from the game image
//   ret f1                 the function returns a float: f1 is compared bit for bit
//   fval 219999998.4292    an edge value for f1..f4 (a quarter of the time one of
//                          these, nudged by -2..+2 ulps), e.g. a cutoff
// Float inputs otherwise: half fixed specials (+-0, +-inf, NaNs, denormals, ...),
// a quarter uniform in -2048..2048, a quarter log-uniform 2^-60..2^60.
// FPU: ctx.fpscr caches the host MXCSR. Each case starts from the default MXCSR
// with the flush (FTZ/DAZ) bits randomly on or off, host and cache equal; after
// the call ctx.fpscr and the host MXCSR must match between the two versions.
// Stack: the ABI check compares only [r1, top). Memory below r1 (the red zone)
// is dead after a return, so differences there count as strict-only.
#include "fable_2_pch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <bit>
#include <cmath>
#include <sys/mman.h>
#include <vector>

// Logging stubs: the harness doesn't link the SDK. ppc_trap (twi/tw) only logs and
// carries on, so every log call is counted and the counts must match per case.
namespace rex {
long g_log_calls = 0;
LogCategoryId RegisterLogCategory(const char*) { return LogCategoryId(1); }
spdlog::logger* GetLoggerRaw(LogCategoryId) { ++g_log_calls; return nullptr; }
}  // namespace rex

extern "C" REX_FUNC(ORIGINAL);
#ifdef MUTANTS
// tools/theseus/mutate.py: many mutants of the original in one binary; argv[3]
// picks one. Each mutant stands in for the rewrite.
using GuestFn = void (*)(PPCContext&, uint8_t*);
extern "C" const GuestFn kMutantTable[];
extern "C" const int kMutantCount;
static GuestFn g_rewrite = nullptr;
#define CALL_REWRITE(c, m) g_rewrite(c, m)
#else
extern "C" REX_FUNC(REWRITE);
#define CALL_REWRITE(c, m) REWRITE(c, m)
#endif

namespace {

constexpr uint64_t kGuestSize = 0x100000000ull + 0x10000;
constexpr uint32_t kWindow = 0x40000000, kWindowSize = 0x10000;
// A copy of the window above the sign bit (real guest pointers live on both sides:
// heap 0x4..., image 0x82..., physical 0xA0...+). A quarter of the random pointers
// go there, so a rewrite that compares pointers as signed numbers fails.
// (Mutants found this gap: B1 in plans/THESEUS_BATCH.md.) Below 0xE0000000,
// where the SDK adds its 4 KB physical-mirror offset.
constexpr uint32_t kHiDelta = 0x70000000, kWindowHi = kWindow + kHiDelta;
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

enum Kind { kPtr, kPtrOrNull, kLen, kInt, kU32, kAny, kNear, kSLen, kTreeCont, kTreeNode, kTreeKey, kFixed, kStr, kPtrLo };
struct ArgRule { int reg; Kind kind; uint32_t max; int base = 0; };
struct MemRule { uint32_t addr; Kind kind; uint32_t max; int base = 0; uint32_t deref = 0;
                 int near_base = 0; uint32_t near_off = 0; };  // kNear: half the time *(r<near_base>+near_off) + 0..max  // base: rN / deref: 32-bit global holding the base; addr = offset
struct TreeRule { uint32_t isnil_off, key_off, max_nodes; };  // max_nodes 0: no tree
struct DataRule { uint32_t addr; const char* hex; };  // addr 0: unused
#ifdef HAS_RULES
#include "difftest_rules.inc"
#else
const ArgRule kRules[] = {{0, kAny, 0}};  // r0: never an argument, so unused
const MemRule kMemRules[] = {{0, kAny, 0}};  // address 0: unused
const TreeRule kTreeRule = {0, 0, 0};
const DataRule kDataRules[] = {{0u, ""}};
constexpr bool kFloatReturn = false;
const double kFloatValues[] = {0.0};  // fval rules; a lone +0.0 = none
#endif
constexpr size_t kFloatValueCount = sizeof(kFloatValues) / sizeof(kFloatValues[0]);

// ---- tree rule: a valid MSVC _Tree built into the window buffer -------------
constexpr uint32_t kTreeArea = kWindow + 0xC000;  // last 16 KB of the window
constexpr uint32_t kNodeStride = 32, kMaxNodes = 256;
constexpr uint32_t kTreeContAddr = kTreeArea;                        // container struct
constexpr uint32_t kTreeKeySlot = kTreeArea + 0x10;             // a key for tree_key
constexpr uint32_t kTreeHead = kTreeArea + 0x20;                 // nodes follow the head

struct TreeState { uint32_t n = 0; uint32_t keys[kMaxNodes]; };
TreeState g_tree;

void put32(std::vector<uint8_t>& w, uint32_t addr, uint32_t v) {
  uint8_t* p = w.data() + (addr - kWindow);
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
uint32_t node_addr(uint32_t i) { return kTreeHead + (i + 1) * kNodeStride; }

// Links keys[lo, hi) as a random-shaped BST under `parent`; returns its root (or head).
uint32_t build_subtree(std::vector<uint8_t>& w, Rng& r, uint32_t lo, uint32_t hi, uint32_t parent) {
  if (lo >= hi) return kTreeHead;
  const uint32_t mid = lo + r.below(hi - lo), node = node_addr(mid);
  put32(w, node + 4, parent);
  put32(w, node + 0, build_subtree(w, r, lo, mid, node));
  put32(w, node + 8, build_subtree(w, r, mid + 1, hi, node));
  return node;
}

void build_tree(std::vector<uint8_t>& w, Rng& r) {
  const TreeRule& t = kTreeRule;
  TreeState& s = g_tree;
  s.n = r.below(t.max_nodes + 1);
  // Sorted distinct keys; sometimes clustered (small range) so +-1 hits neighbours.
  const uint32_t range = r.below(2) ? 0xFFFFFFFFu : 4 * s.n + 4;
  for (uint32_t i = 0; i < s.n; ++i) s.keys[i] = uint32_t(r.next() % range);
  std::sort(s.keys, s.keys + s.n);
  s.n = uint32_t(std::unique(s.keys, s.keys + s.n) - s.keys);
  for (uint32_t i = 0; i < s.n; ++i) {
    put32(w, node_addr(i) + t.key_off, s.keys[i]);
    w[node_addr(i) + t.isnil_off - kWindow] = 0;
  }
  w[kTreeHead + t.isnil_off - kWindow] = 1;
  const uint32_t root = build_subtree(w, r, 0, s.n, kTreeHead);
  put32(w, kTreeHead + 4, root);
  put32(w, kTreeHead + 0, s.n ? node_addr(0) : kTreeHead);         // leftmost
  put32(w, kTreeHead + 8, s.n ? node_addr(s.n - 1) : kTreeHead);   // rightmost
  put32(w, kTreeContAddr + 0, uint32_t(r.next()) | 1);                 // proxy / allocator
  put32(w, kTreeContAddr + 4, kTreeHead);
  put32(w, kTreeContAddr + 8, s.n);
  uint32_t key = uint32_t(r.next());
  if (s.n && r.below(4) != 0) {
    key = s.keys[r.below(s.n)];
    if (r.below(2)) key += r.below(2) ? 1u : 0xFFFFFFFFu;
  }
  put32(w, kTreeKeySlot, key);
}

uint32_t window_ptr(Rng& r, uint32_t room) {
  const uint32_t p = kWindow + r.below(room);
  return r.below(4) == 0 ? p + kHiDelta : p;
}

uint64_t ruled_value(Rng& r, const ArgRule& rule) {
  switch (rule.kind) {
    case kPtr: case kStr: return window_ptr(r, kWindowSize / 2);
    case kPtrLo: return kWindow + r.below(kWindowSize / 2);  // `ptr_lo`: never the high copy
    case kPtrOrNull: return r.below(8) == 0 ? 0 : window_ptr(r, kWindowSize / 2);
    case kLen: return r.below(rule.max + 1);
    case kSLen: return uint64_t(int64_t(r.below(2 * rule.max + 1)) - int64_t(rule.max));
    case kInt: return uint64_t(int64_t(int32_t(r.next())));
    case kU32: return uint32_t(r.next());
    case kTreeCont: return kTreeContAddr;
    case kTreeNode: return g_tree.n && r.below(16) ? node_addr(r.below(g_tree.n)) : kTreeHead;
    case kTreeKey: return kTreeKeySlot;
    case kFixed: return rule.max;
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
    if (i * kPage >= kStackTop - kStackSize && i * kPage < kStackTop) continue;  // compared per case
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
    case 0: case 1: return window_ptr(r, kWindowSize - 64);       // pointer into data
    case 2: return r.below(64);                                   // small int / length
    case 3: return uint32_t(r.next());                            // any 32-bit value
    case 4: return uint64_t(int64_t(int32_t(r.next())));          // sign-extended 32-bit
    default: return r.next();                                     // any 64-bit value
  }
}

double fp_value(Rng& r) {
  static const double nice[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, 1e-30, 1e30, 3.14159,
                                1e18, -1e18, 1e18 + 65536.0, 4503599627370496.0, 4503599627370497.0,
                                -2.5, 1e-310, -1e-310, 1.17549435e-38,
                                __builtin_inf(), -__builtin_inf(), __builtin_nan(""), -__builtin_nan(""), __builtin_nans("5")};
  if (kFloatValues[0] != 0.0 || std::signbit(kFloatValues[0]) || kFloatValueCount > 1) {
    if (r.below(4) == 0) {  // a per-function edge value, nudged by -2..+2 ulps
      double v = kFloatValues[r.below(kFloatValueCount)];
      int steps = int(r.below(5)) - 2;
      for (; steps > 0; --steps) v = std::nextafter(v, __builtin_inf());
      for (; steps < 0; ++steps) v = std::nextafter(v, -__builtin_inf());
      return v;
    }
  }
  if (r.below(2)) return nice[r.below(sizeof(nice) / sizeof(nice[0]))];
  if (r.below(2)) return (double(int64_t(r.next() >> 11)) / double(1ull << 40)) - 2048.0;
#ifdef NO_LOG_UNIFORM  // ablation (mutate.py --ablate): the pilot's float inputs before #16
  return (double(int64_t(r.next() >> 11)) / double(1ull << 40)) - 2048.0;
#endif
  // log-uniform magnitude 2^-60 .. 2^60, random sign: covers large and tiny inputs
  const double m = 1.0 + double(r.next() >> 11) / double(1ull << 53);
  const double v = std::ldexp(m, int(r.below(121)) - 60);
  return r.below(2) ? -v : v;
}

struct Field { const char* name; size_t off, size; bool abi; };  // abi of f1: kFloatReturn
#define F(m, abi) Field{#m, offsetof(PPCContext, m), sizeof(PPCContext::m), abi}
Field kFields[] = {
  F(r0, 0), F(r1, 1), F(r2, 1), F(r4, 0), F(r5, 0), F(r6, 0), F(r7, 0), F(r8, 0),
  F(r9, 0), F(r10, 0), F(r11, 0), F(r12, 0), F(r13, 1), F(r14, 1), F(r15, 1),
  F(r16, 1), F(r17, 1), F(r18, 1), F(r19, 1), F(r20, 1), F(r21, 1), F(r22, 1),
  F(r23, 1), F(r24, 1), F(r25, 1), F(r26, 1), F(r27, 1), F(r28, 1), F(r29, 1),
  F(r30, 1), F(r31, 1), F(lr, 1), F(ctr, 0), F(xer, 0), F(reserved, 0),
  F(cr0, 0), F(cr1, 0), F(cr2, 1), F(cr3, 1), F(cr4, 1), F(cr5, 0), F(cr6, 0), F(cr7, 0),
  F(fpscr, 1), F(f0, 0), F(f1, 0), F(f2, 0), F(f3, 0), F(f4, 0), F(f5, 0), F(f6, 0),
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
#ifdef MUTANTS
  const int mutant = argc > 3 ? std::atoi(argv[3]) : -1;
  if (mutant < 0 || mutant >= kMutantCount) {
    std::printf("mutants=%d (pass an index 0..%d as the third argument)\n", kMutantCount, kMutantCount - 1);
    return 2;
  }
  g_rewrite = kMutantTable[mutant];
#endif
  // DIFFTEST_STOP=1: stop at the first ABI failure (mutant runs only need to know that).
  const bool stop_early = std::getenv("DIFFTEST_STOP") != nullptr;
  for (Field& f : kFields)
    if (f.off == offsetof(PPCContext, f1)) f.abi = kFloatReturn;
  const uint32_t default_csr = 0x1F80;  // all exceptions masked, round to nearest
  long csr_bad = 0;
  uint8_t* mem_a = map_guest();
  uint8_t* mem_b = map_guest();
  Rng rng{seed};
  std::vector<uint8_t> window(kWindowSize), stack(kStackSize);

  long strict_bad = 0, abi_bad = 0, r3_low_bad = 0, r3_high_bad = 0, mem_bad = 0, redzone_bad = 0;
  long trap_bad = 0, traps_a = 0;
  long field_bad[sizeof(kFields) / sizeof(kFields[0])] = {};
  long first_abi_case = -1;
  constexpr long kScanEvery = 1000;
  long stray_pages = 0, first_stray_scan = -1;
  uint32_t first_stray_page = 0;

  for (long c = 0; c < cases; ++c) {
    for (auto& b : window) b = rng.below(12) == 0 ? 0 : uint8_t(rng.next());
    for (auto& b : stack) b = uint8_t(rng.next());
    if (kTreeRule.max_nodes) build_tree(window, rng);
    for (uint32_t w : {kWindow, kWindowHi}) {
      std::memcpy(mem_a + w, window.data(), kWindowSize);
      std::memcpy(mem_b + w, window.data(), kWindowSize);
    }
    std::memcpy(mem_a + kStackTop - kStackSize, stack.data(), kStackSize);
    std::memcpy(mem_b + kStackTop - kStackSize, stack.data(), kStackSize);

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
    for (const ArgRule& rule : kRules)  // `rN str <max>`: a string of a chosen length at rN
      if (rule.kind == kStr) {
        // Lengths 0..max, a quarter of them at the edges (max-1, max, max+1) so a
        // length cap inside the function is reached; bytes 1..255 (signed chars too).
        uint32_t len = rng.below(4) == 0 ? rule.max - 1 + rng.below(3) : rng.below(rule.max + 1);
        const uint32_t at = args[rule.reg - 3]->u32;
        for (uint32_t i = 0; i <= len; ++i) {
          const uint8_t ch = i == len ? 0 : uint8_t(1 + rng.below(255));
          mem_a[at + i] = mem_b[at + i] = ch;
        }
      }
    for (PPCRegister* f : {&init.f1, &init.f2, &init.f3, &init.f4})
      f->f64 = fp_value(rng);
    for (const MemRule& m : kMemRules) {
      if (!m.addr && !m.base && !m.deref) continue;
      uint32_t addr = m.base ? args[m.base - 3]->u32 + m.addr : m.addr;
      if (m.deref) {
        const uint8_t* g = mem_a + m.deref;
        addr = ((uint32_t(g[0]) << 24) | (uint32_t(g[1]) << 16) | (uint32_t(g[2]) << 8) | g[3]) + m.addr;
      }
      uint32_t v;
      if (m.kind == kNear) {  // `mem32 @r3+8 near @r3+12 512`: a field close to another field's value
        const uint8_t* q = mem_a + args[m.near_base - 3]->u32 + m.near_off;
        const uint32_t ref = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
        // The other half: anywhere in the SAME window as the referenced value, so two
        // fields of one struct (e.g. Lua's top and base) never straddle the windows
        // (a loop from one to the other would sweep 1.8 GB).
        const uint32_t lo = kWindow + rng.below(kWindowSize / 2);
        v = rng.below(2) ? ref + rng.below(m.max + 1) : (ref >= kWindowHi ? lo + kHiDelta : lo);
      } else {
        v = uint32_t(ruled_value(rng, {0, m.kind, m.max}));
      }
      const uint8_t be[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
      std::memcpy(mem_a + addr, be, 4);
      std::memcpy(mem_b + addr, be, 4);
    }
    for (const DataRule& d : kDataRules) {
      if (!d.addr) continue;
      for (size_t i = 0; d.hex[2 * i]; ++i) {
        const char byte[3] = {d.hex[2 * i], d.hex[2 * i + 1], 0};
        mem_a[d.addr + i] = mem_b[d.addr + i] = uint8_t(std::strtoul(byte, nullptr, 16));
      }
    }
    std::memset(&init.fpscr, 0, sizeof(init.fpscr));
    init.fpscr.csr = default_csr | (rng.below(2) ? uint32_t(init.fpscr.FlushMask) : 0u);
    init.r1.u64 = kStackTop - 0x100;
    init.xer.so = rng.below(2); init.xer.ov = rng.below(2); init.xer.ca = rng.below(2);

    PPCContext a = init, b = init;
    const long log0 = rex::g_log_calls;
    a.fpscr.setcsr(init.fpscr.csr);
    ORIGINAL(a, mem_a);
    const uint32_t host_csr_a = a.fpscr.getcsr();
    const long log_a = rex::g_log_calls - log0;
    b.fpscr.setcsr(init.fpscr.csr);
    CALL_REWRITE(b, mem_b);
    const uint32_t host_csr_b = b.fpscr.getcsr();
    a.fpscr.setcsr(default_csr);
    const bool csr_same = (host_csr_a & ~0x3Fu) == (host_csr_b & ~0x3Fu);  // control bits; 0-5 are sticky flags
    csr_bad += !csr_same;
    const long log_b = rex::g_log_calls - log0 - log_a;
    traps_a += log_a;
#ifdef NO_TRAP_COUNT  // ablation (mutate.py --ablate): before trap counting existed
    bool traps_same = true;
#else
    bool traps_same = log_a == log_b;
#endif
    trap_bad += !traps_same;

    const uint32_t sp = uint32_t(init.r1.u64);
    bool memory_same =
        std::memcmp(mem_a + kWindow, mem_b + kWindow, kWindowSize) == 0 &&
        std::memcmp(mem_a + kWindowHi, mem_b + kWindowHi, kWindowSize) == 0 &&
        std::memcmp(mem_a + sp, mem_b + sp, kStackTop - sp) == 0;
    bool redzone_same = std::memcmp(mem_a + kStackTop - kStackSize, mem_b + kStackTop - kStackSize,
                                    sp - (kStackTop - kStackSize)) == 0;
    redzone_bad += !redzone_same;
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
    bool abi_ok = memory_same && low_same && abi_fields_same && traps_same && csr_same;
    bool strict_ok = abi_ok && high_same && redzone_same && std::memcmp(&a, &b, sizeof(a)) == 0;
    strict_bad += !strict_ok;
    abi_bad += !abi_ok;
    r3_low_bad += !low_same;
    r3_high_bad += !high_same;
    mem_bad += !memory_same;
    if (!abi_ok && first_abi_case < 0) {
      first_abi_case = c;
      std::printf("first ABI failure (case %ld): r3 %016llx|%016llx -> %016llx|%016llx f1 in %016llx out %016llx|%016llx\n",
                  c, (unsigned long long)init.r3.u64, (unsigned long long)init.r4.u64,
                  (unsigned long long)a.r3.u64, (unsigned long long)b.r3.u64,
                  (unsigned long long)init.f1.u64, (unsigned long long)a.f1.u64, (unsigned long long)b.f1.u64);
      if (init.r3.u32 >= kWindow && init.r3.u32 < kWindow + kWindowSize - 32) {
        std::printf("  input bytes at r3&~15:");
        for (uint32_t i = 0; i < 32; ++i) std::printf("%s%02x", i % 4 ? "" : " ", window[(init.r3.u32 & ~15u) - kWindow + i]);
        std::printf("\n");
      }
      for (uint32_t off = 0; off < kWindowSize; off += 4)
        if (std::memcmp(mem_a + kWindow + off, mem_b + kWindow + off, 4) != 0) {
          uint32_t before, wa, wb;
          std::memcpy(&before, window.data() + off, 4);
          std::memcpy(&wa, mem_a + kWindow + off, 4);
          std::memcpy(&wb, mem_b + kWindow + off, 4);
          std::printf("  mem %08x: was %08x original %08x rewrite %08x (host byte order)\n",
                      kWindow + off, before, wa, wb);
        }
    }
    if ((c + 1) % kScanEvery == 0 || c + 1 == cases) {
      long n = scan_touched(mem_a, mem_b, first_stray_scan < 0 ? &first_stray_page : nullptr);
      if (n && first_stray_scan < 0) first_stray_scan = c;
      stray_pages += n;
    }
    if (stop_early && (abi_bad || stray_pages)) {
      if (!stray_pages) stray_pages = scan_touched(mem_a, mem_b, &first_stray_page);
      std::printf("stopped early after case %ld (DIFFTEST_STOP)\n", c);
      break;
    }
  }

  std::printf("cases=%ld seed=0x%llx\n", cases, (unsigned long long)seed);
  std::printf("abi_mismatches=%ld strict_mismatches=%ld\n", abi_bad, strict_bad);
  std::printf("r3_low32_mismatches=%ld r3_high32_mismatches=%ld memory_mismatches=%ld"
              " redzone_mismatches=%ld (below r1, strict only)\n",
              r3_low_bad, r3_high_bad, mem_bad, redzone_bad);
  std::printf("trap_or_log_count_mismatches=%ld (original logged %ld times)\n", trap_bad, traps_a);
  std::printf("host_mxcsr_mismatches=%ld float_return=%s\n", csr_bad, kFloatReturn ? "f1" : "no");
  std::printf("differing_fields:");
  for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i)
    if (field_bad[i]) std::printf(" %s%s=%ld", kFields[i].name, kFields[i].abi ? "(ABI!)" : "", field_bad[i]);
  std::printf("\n");
  if (first_abi_case >= 0) std::printf("first_abi_failure_case=%ld\n", first_abi_case);
  std::printf("touched_page_mismatches=%ld", stray_pages);
  if (first_stray_scan >= 0)
    std::printf(" first_at_page=0x%08x by_case<=%ld", first_stray_page, first_stray_scan);
  std::printf(" (all touched pages compared every %ld cases)\n", kScanEvery);
  std::printf("rules=%s%s\n", sizeof(kRules) / sizeof(kRules[0]) > 1 || kRules[0].reg || kMemRules[0].addr || kDataRules[0].addr ? "yes" : "none",
              kTreeRule.max_nodes ? " tree" : "");
  const bool fail = abi_bad || stray_pages;
  std::printf("RESULT %s\n", fail ? "FAIL" : (strict_bad ? "PASS-ABI" : "PASS-STRICT"));
  return fail ? 1 : 0;
}

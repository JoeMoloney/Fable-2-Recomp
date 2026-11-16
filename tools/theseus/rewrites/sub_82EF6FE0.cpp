// Readable rewrite of sub_82EF6FE0 (guest 0x82EF6FE0, 26 PPC instructions, 69 callers).
// Gate: python3 tools/theseus/gate.py sub_82EF6FE0
//
// Initialises an object in place: +0 vtable 0x8203D104, +12 = 0. If +4/+8 already
// hold the magic pair 0x56471E89 / 0x9FE1234A, sets +4 = 1 and +8 = 0x832FE170;
// otherwise +4 = 0 and +8 = 0x832FE168.
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kVtable = 0x8203D104;
constexpr uint32_t kMagic1 = 0x56471E89, kMagic2 = 0x9FE1234A;
constexpr uint32_t kTable = 0x832FE168;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82EF6FE0) {
  const uint32_t obj = ctx.r3.u32;
  const uint32_t word4 = REX_LOAD_U32(obj + 4);
  REX_STORE_U32(obj + 12, 0);
  REX_STORE_U32(obj + 0, kVtable);
  const bool magic = word4 == kMagic1 && REX_LOAD_U32(obj + 8) == kMagic2;
  REX_STORE_U32(obj + 4, magic ? 1 : 0);
  REX_STORE_U32(obj + 8, magic ? kTable + 8 : kTable);
}

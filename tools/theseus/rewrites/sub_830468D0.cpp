// Readable rewrite of sub_830468D0 (guest 0x830468D0, 9 PPC instructions, 46 callers).
// Gate: python3 tools/theseus/gate.py sub_830468D0
//
// Constructor: r3 = object { +0 vtable 0x82055568; +4 reference count = 1;
// +8 r4; +12 r5; +16 r6 } (low 32 bits of each argument).
// Name: unknown (constructor of a ref-counted class with vtable 0x82055568).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kVtable = 0x82055568;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_830468D0) {
  const uint32_t obj = ctx.r3.u32;
  REX_STORE_U32(obj + 0, kVtable);
  REX_STORE_U32(obj + 4, 1);
  REX_STORE_U32(obj + 8, ctx.r4.u32);
  REX_STORE_U32(obj + 12, ctx.r5.u32);
  REX_STORE_U32(obj + 16, ctx.r6.u32);
}

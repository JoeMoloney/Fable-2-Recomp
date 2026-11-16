// Readable rewrite of sub_830493B8 (guest 0x830493B8, 26 PPC instructions, 25 callers).
// Gate: python3 tools/theseus/gate.py sub_830493B8
//
// Constructor of an 80-byte object at r3 (returned unchanged): +0 vtable
// 0x82055634, +4 = 14 (a type id), +8/+12/+20/+24 = 0, +16 r4, +28 r5, +32 r6,
// +36 r7, +40 r8, and +48..+79 a copy of the 32 bytes at r9. Stores keep the
// original's order.
// Name: unknown.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kVtable = 0x82055634, kTypeId = 14;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_830493B8) {
  const uint32_t obj = ctx.r3.u32, src = ctx.r9.u32;
  REX_STORE_U32(obj + 28, ctx.r5.u32);
  REX_STORE_U32(obj + 32, ctx.r6.u32);
  REX_STORE_U32(obj + 16, ctx.r4.u32);
  REX_STORE_U32(obj + 8, 0);
  REX_STORE_U32(obj + 4, kTypeId);
  REX_STORE_U32(obj + 0, kVtable);
  REX_STORE_U32(obj + 12, 0);
  REX_STORE_U32(obj + 20, 0);
  REX_STORE_U32(obj + 24, 0);
  REX_STORE_U32(obj + 36, ctx.r7.u32);
  REX_STORE_U32(obj + 40, ctx.r8.u32);
  for (uint32_t off = 0; off < 32; off += 8) REX_STORE_U64(obj + 48 + off, REX_LOAD_U64(src + off));
}

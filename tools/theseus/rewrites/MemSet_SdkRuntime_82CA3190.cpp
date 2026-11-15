// Readable rewrite of MemSet_SdkRuntime_82CA3190 (guest 0x82CA3190, 40 PPC
// instructions, 479 callers). Test: python3 tools/theseus/difftest.py MemSet_SdkRuntime_82CA3190
//
// C runtime memset: r3 = destination, r4 = value (low byte used), r5 = count
// (low 32 bits). Returns the destination (r3 unchanged).
// The original is speed-tuned (bytes until 4-byte aligned, then 16-byte blocks,
// 4-byte words, leftover bytes); the bytes it writes are the same as a plain
// loop's, so the rewrite is the plain loop.
#include "fable_2_pch.h"

namespace {

void MemSet(uint8_t* base, uint32_t dst, uint8_t value, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) REX_STORE_U8(dst + i, value);
}

}  // namespace

extern "C" REX_FUNC(rewrite_MemSet_SdkRuntime_82CA3190) {
  MemSet(base, ctx.r3.u32, ctx.r4.u8, ctx.r5.u32);
}

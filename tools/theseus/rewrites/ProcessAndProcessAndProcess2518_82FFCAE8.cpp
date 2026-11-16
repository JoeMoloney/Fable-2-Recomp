// Readable rewrite of ProcessAndProcessAndProcess2518_82FFCAE8 (guest 0x82FFCAE8,
// 11 PPC instructions, 27 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess2518_82FFCAE8
//
// Unoptimised (debug-built) pair constructor: p = r3 gets { +0 r4, +4 r5 } and
// is returned (low 32 bits). Like all unoptimised code it first spills its three
// arguments to their home slots in the caller's frame (r1 + 20 / 28 / 36), and
// those stores are part of the contract (the caller's stack is compared).
// Proposed name: Pair_Construct_82FFCAE8.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess2518_82FFCAE8) {
  const uint32_t sp = ctx.r1.u32;
  REX_STORE_U32(sp + 20, ctx.r3.u32);
  REX_STORE_U32(sp + 28, ctx.r4.u32);
  REX_STORE_U32(sp + 36, ctx.r5.u32);
  const uint32_t pair = ctx.r3.u32;
  REX_STORE_U32(pair + 0, ctx.r4.u32);
  REX_STORE_U32(pair + 4, ctx.r5.u32);
  ctx.r3.u64 = pair;
}

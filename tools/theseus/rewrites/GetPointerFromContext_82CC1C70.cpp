// Readable rewrite of GetPointerFromContext_82CC1C70 (guest 0x82CC1C70, 8 PPC
// instructions, 21 callers). Gate: python3 tools/theseus/gate.py GetPointerFromContext_82CC1C70
//
// Reads through r13, the Xbox 360 per-processor block (KPCR): returns
// *(current_thread + 352) where current_thread = *(r13 + 256) (KPCR+0x100), or 0
// while the word at KPCR+0x150 is non-zero (likely "in a DPC/interrupt": no
// thread context to use).
// Proposed name: GetCurrentThreadField160_82CC1C70.
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kPcrBusyFlag = 336;      // KPCR+0x150
constexpr uint32_t kPcrCurrentThread = 256; // KPCR+0x100
constexpr uint32_t kThreadField = 352;      // KTHREAD+0x160

uint32_t CurrentThreadField(uint8_t* base, uint32_t pcr) {
  if (REX_LOAD_U32(pcr + kPcrBusyFlag) != 0) return 0;
  return REX_LOAD_U32(REX_LOAD_U32(pcr + kPcrCurrentThread) + kThreadField);
}

}  // namespace

extern "C" REX_FUNC(rewrite_GetPointerFromContext_82CC1C70) {
  ctx.r3.u64 = CurrentThreadField(base, ctx.r13.u32);
}

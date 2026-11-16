// Readable rewrite of sub_824FDA50 (guest 0x824FDA50, 21 PPC instructions, 12 callers).
// Gate: python3 tools/theseus/gate.py sub_824FDA50
//
// Lua binding TutorialManager.HasDisplayedTutorial: r3 = binding object whose
// +4 points at the tutorial manager; the manager holds a vector of 8-byte
// entries (begin +16, end +20) whose first word points at a tutorial record
// { +0 int id; +40 int times displayed }. Returns 1 when the tutorial with id
// r4 is found and has been displayed (count > 0), else 0.
// Proposed name: TutorialManager_HasDisplayedTutorial_824FDA50 (evidence.py, high:
// registered as the Lua binding TutorialManager.HasDisplayedTutorial).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kManager = 4, kBegin = 16, kEnd = 20, kEntrySize = 8;
constexpr uint32_t kTutId = 0, kTutDisplayCount = 40;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_824FDA50) {
  const uint32_t mgr = REX_LOAD_U32(ctx.r3.u32 + kManager);
  const uint32_t end = REX_LOAD_U32(mgr + kEnd);
  for (uint32_t it = REX_LOAD_U32(mgr + kBegin); it != end; it += kEntrySize) {
    const uint32_t tut = REX_LOAD_U32(it);
    if (int32_t(REX_LOAD_U32(tut + kTutId)) == ctx.r4.s32) {
      ctx.r3.u64 = int32_t(REX_LOAD_U32(tut + kTutDisplayCount)) > 0 ? 1 : 0;
      return;
    }
  }
  ctx.r3.u64 = 0;
}

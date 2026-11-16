// Readable rewrite of sub_82214C28 (guest 0x82214C28, 41 PPC instructions,
// 117 callers). Gate: python3 tools/theseus/gate.py sub_82214C28
//
// A yes/no check (returns 1 or 0 in r3) on global state plus one flag of the
// object in r3:
//   g      = *(0x83496920)          a global manager
//   state  = *(g + 12)
//   flags  = *(state + 140)         three bytes at +52 (enabled), +53, +54
//   mode   = *(state + 284)
// Returns 1 if the flags block is disabled (+52 == 0), or if +53 is set and the
// object's byte +104 is clear, or if +54 is set and that byte is set;
// otherwise returns mode == 2.
// Proposed name: none yet (unnamed callers across 0x8217..0x826D).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kManagerGlobal = 0x83496920;
constexpr uint32_t kManagerState = 12, kStateFlags = 140, kStateMode = 284;
constexpr uint32_t kFlagsEnabled = 52, kFlagsWhenClear = 53, kFlagsWhenSet = 54;
constexpr uint32_t kObjectFlag = 104;
constexpr int32_t kModeAlways = 2;

bool Check(uint8_t* base, uint32_t object) {
  const uint32_t manager = REX_LOAD_U32(kManagerGlobal);
  const uint32_t state = REX_LOAD_U32(manager + kManagerState);
  const uint32_t flags = REX_LOAD_U32(state + kStateFlags);
  if (REX_LOAD_U8(flags + kFlagsEnabled) == 0) return true;
  const bool object_flag = REX_LOAD_U8(object + kObjectFlag) != 0;
  if (REX_LOAD_U8(flags + kFlagsWhenClear) != 0 && !object_flag) return true;
  if (REX_LOAD_U8(flags + kFlagsWhenSet) != 0 && object_flag) return true;
  return int32_t(REX_LOAD_U32(state + kStateMode)) == kModeAlways;
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82214C28) {
  ctx.r3.u64 = Check(base, ctx.r3.u32) ? 1 : 0;
}

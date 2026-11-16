// Readable rewrite of GetField833_822F8628 (guest 0x822F8628, 8 PPC
// instructions, 28 callers). Gate: python3 tools/theseus/gate.py GetField833_822F8628
//
// c_str() of a string handle: r3 points to a pointer to the string's buffer
// (whose first word is the character pointer). Returns that character pointer,
// or "" when the handle is null. The "" is the byte at 0x8209003F (the shared
// tail of another literal; Ghidra shows 00 there), returned sign-extended
// (lis/addi), as the original does.
// Callers read r6/r9/r10 after the call (they know this leaf leaves them alone):
// this rewrite only writes r3.
// Proposed name: StringHandle_CStr_822F8628.
#include "fable_2_pch.h"

namespace {

constexpr int64_t kEmptyString = int32_t(0x8209003F);  // "" in the image, sign-extended

uint64_t CStr(uint8_t* base, uint32_t handle) {
  const uint32_t buffer = REX_LOAD_U32(handle);
  if (buffer == 0) return uint64_t(kEmptyString);
  return REX_LOAD_U32(buffer);
}

}  // namespace

extern "C" REX_FUNC(rewrite_GetField833_822F8628) {
  ctx.r3.u64 = CStr(base, ctx.r3.u32);
}

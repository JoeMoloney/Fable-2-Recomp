// Readable rewrite of sub_82BFFAA8 (guest 0x82BFFAA8, 8 PPC instructions,
// 23 callers, e.g. FrontMenu_EnsureElement). Gate: python3 tools/theseus/gate.py sub_82BFFAA8
//
// c_str() of a string handle stored at +4 of the object in r3: returns the
// character pointer (first word of the buffer), or "" (the byte at 0x8209003F,
// sign-extended as lis/addi leave it) when the handle is null. Same pattern as
// GetField833_822F8628, one field further in.
// Proposed name: GetNameCStr_82BFFAA8 (UI callers; the +4 field is likely a name).
#include "fable_2_pch.h"

namespace {

constexpr uint32_t kNameHandle = 4;
constexpr int64_t kEmptyString = int32_t(0x8209003F);  // "" in the image, sign-extended

uint64_t NameCStr(uint8_t* base, uint32_t object) {
  const uint32_t buffer = REX_LOAD_U32(object + kNameHandle);
  if (buffer == 0) return uint64_t(kEmptyString);
  return REX_LOAD_U32(buffer);
}

}  // namespace

extern "C" REX_FUNC(rewrite_sub_82BFFAA8) {
  ctx.r3.u64 = NameCStr(base, ctx.r3.u32);
}

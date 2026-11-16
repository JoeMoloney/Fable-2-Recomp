// Readable rewrite of sub_83079640 (guest 0x83079640, 10 PPC instructions, 57 callers).
// Gate: python3 tools/theseus/gate.py sub_83079640
//
// Copies the word at +60 from r4 to r3 and returns S_OK (0); with r4 == null
// returns E_FAIL (0x80004005, sign-extended) and copies nothing.
// Name: unknown (an HRESULT-returning field copy).
#include "fable_2_pch.h"

namespace {
constexpr int64_t kEFail = int32_t(0x80004005);
constexpr uint32_t kField = 60;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_83079640) {
  const uint32_t src = ctx.r4.u32;
  if (src == 0) {
    ctx.r3.s64 = kEFail;
    return;
  }
  REX_STORE_U32(ctx.r3.u32 + kField, REX_LOAD_U32(src + kField));
  ctx.r3.u64 = 0;
}

// Readable rewrite of Ctype_Pred_Bit3_82CA6B50 (guest 0x82CA6B50, 7 PPC
// instructions, 14 callers). Gate: python3 tools/theseus/gate.py Ctype_Pred_Bit3_82CA6B50
//
// MSVC CRT isspace(c): the locale ctype table (_pctype, pointer at 0x832EF350)
// entry for c, masked with _SPACE (0x8). Index = low 32 bits of c times 2.
// Proposed name: isspace_82CA6B50 (bit 3 = _SPACE; same table as isalpha_82CA6AB0).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kPctype = 0x832EF350;
constexpr uint32_t kSpace = 0x8;
}  // namespace

extern "C" REX_FUNC(rewrite_Ctype_Pred_Bit3_82CA6B50) {
  const uint32_t table = REX_LOAD_U32(kPctype);
  ctx.r3.u64 = REX_LOAD_U16(table + ctx.r3.u32 * 2) & kSpace;
}

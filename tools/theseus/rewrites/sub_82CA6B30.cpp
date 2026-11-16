// Readable rewrite of sub_82CA6B30 (guest 0x82CA6B30, 7 PPC instructions, 9 callers).
// Gate: python3 tools/theseus/gate.py sub_82CA6B30
//
// MSVC CRT isxdigit(c): the locale ctype table (_pctype, pointer at 0x832EF350)
// entry for c, masked with _HEX (0x80). Index = low 32 bits of c times 2.
// Proposed name: isxdigit_82CA6B30 (bit 7 = _HEX; same table as isalpha_82CA6AB0).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kPctype = 0x832EF350;
constexpr uint32_t kHex = 0x80;
}  // namespace

extern "C" REX_FUNC(rewrite_sub_82CA6B30) {
  const uint32_t table = REX_LOAD_U32(kPctype);
  ctx.r3.u64 = REX_LOAD_U16(table + ctx.r3.u32 * 2) & kHex;
}

// Readable rewrite of ProcessAndProcessAndProcess1012_82CA6AB0 (guest 0x82CA6AB0,
// 7 PPC instructions, 20 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess1012_82CA6AB0
//
// MSVC CRT isalpha(c): looks c up in the locale's ctype table (u16 per char,
// pointer in a CRT global at 0x832EF350, i.e. _pctype) and masks
// _UPPER|_LOWER|_ALPHA (0x103). The index is the low 32 bits of c times 2.
// Proposed name: isalpha_82CA6AB0 (mask 0x103 = _ALPHA|_LOWER|_UPPER; the
// neighbours at 0x82CA6B30.. test other ctype bits).
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kPctype = 0x832EF350;   // const unsigned short* _pctype
constexpr uint32_t kAlphaMask = 0x103;     // _UPPER | _LOWER | _ALPHA
}  // namespace

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess1012_82CA6AB0) {
  const uint32_t table = REX_LOAD_U32(kPctype);
  ctx.r3.u64 = REX_LOAD_U16(table + ctx.r3.u32 * 2) & kAlphaMask;
}

// Readable rewrite of LookupMasked_8223F970 (guest 0x8223F970, 7 PPC
// instructions, 9 callers). Gate: python3 tools/theseus/gate.py LookupMasked_8223F970
//
// MSVC CRT isleadbyte(c): the current locale info (pointer at 0x832EF368) has
// its ctype table at +200; returns that table's entry for the low byte of c
// masked with _LEADBYTE (0x8000).
// Proposed name: isleadbyte_8223F970.
#include "fable_2_pch.h"

namespace {
constexpr uint32_t kLocaleInfo = 0x832EF368;
constexpr uint32_t kCtypeTable = 200;
constexpr uint32_t kLeadByte = 0x8000;
}  // namespace

extern "C" REX_FUNC(rewrite_LookupMasked_8223F970) {
  const uint32_t table = REX_LOAD_U32(REX_LOAD_U32(kLocaleInfo) + kCtypeTable);
  ctx.r3.u64 = REX_LOAD_U16(table + (ctx.r3.u32 & 0xFF) * 2) & kLeadByte;
}

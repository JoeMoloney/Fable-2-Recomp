// Readable rewrite of SwitchDispatch_82C8DAE8 (guest 0x82C8DAE8, 22 PPC
// instructions, 48 callers). Gate: python3 tools/theseus/gate.py SwitchDispatch_82C8DAE8
//
// expat's unicode_byte_type(hi, lo) (xmltok.c): the byte type of a UTF-16 code
// unit that is not ASCII, from its high byte r3 and low byte r4 (low 8 bits each).
//   0xD8..0xDB -> BT_LEAD4 (7)    high surrogate: starts a 4-byte sequence
//   0xDC..0xDF -> BT_TRAIL (8)    low surrogate
//   0xFFFE, 0xFFFF -> BT_NONXML (0)
//   anything else -> BT_NONASCII (29)
// Evidence: the exact expat values and cases; the callers (0x82C8E7A0..) form a
// block of tokenizer functions (expat's big2_/little2_ variants).
// Proposed name: Expat_unicode_byte_type_82C8DAE8.
#include "fable_2_pch.h"

namespace {

enum ByteType : int64_t { BT_NONXML = 0, BT_LEAD4 = 7, BT_TRAIL = 8, BT_NONASCII = 29 };

ByteType UnicodeByteType(uint8_t hi, uint8_t lo) {
  if (hi >= 0xD8 && hi <= 0xDB) return BT_LEAD4;
  if (hi >= 0xDC && hi <= 0xDF) return BT_TRAIL;
  if (hi == 0xFF && lo >= 0xFE) return BT_NONXML;
  return BT_NONASCII;
}

}  // namespace

extern "C" REX_FUNC(rewrite_SwitchDispatch_82C8DAE8) {
  ctx.r3.s64 = UnicodeByteType(uint8_t(ctx.r3.u32), uint8_t(ctx.r4.u32));
}

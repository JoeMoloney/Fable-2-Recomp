// Readable rewrite of MemCpy_Aligned_82CA30E8 (guest 0x82CA30E8, 42 PPC
// instructions, 34 callers). Gate: python3 tools/theseus/gate.py MemCpy_Aligned_82CA30E8
//
// CRT memcpy(dst = r3, src = r4, n = r5), returns dst (r3 unchanged). Forward
// copy: single bytes until dst is 4-byte aligned, then 32-bit words (a load then
// a store each, src may be unaligned), then the remaining 0..3 bytes. The order
// is kept, so overlapping copies behave the same.
// Name kept (MemCpy_Aligned); it is the CRT memcpy.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_MemCpy_Aligned_82CA30E8) {
  uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32;
  while (n != 0 && (dst & 3) != 0) {
    REX_STORE_U8(dst++, REX_LOAD_U8(src++));
    --n;
  }
  for (uint32_t words = n >> 2; words != 0; --words) {
    REX_STORE_U32(dst, REX_LOAD_U32(src));
    dst += 4;
    src += 4;
  }
  for (uint32_t tail = n & 3; tail != 0; --tail) REX_STORE_U8(dst++, REX_LOAD_U8(src++));
}

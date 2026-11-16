// Readable rewrite of FlushCache_821D11C8 (guest 0x821D11C8, 41 PPC
// instructions, 24 callers). Gate: python3 tools/theseus/gate.py FlushCache_821D11C8
//
// Data-cache flush of [r3, r4) (start, end) in 128-byte lines (an unrolled dcbst/dcbf
// loop, 8 lines per iteration), skipped for addresses 0x7F100000..0x86FFFFFF.
// The recompiler drops the cache instructions (host caches are coherent), so
// what is left only counts through the lines and changes scratch registers:
// it has no visible effect. This rewrite does nothing; r3 is returned unchanged.
// Speed: the original's recompiled loop still runs once per 1 KB; doing nothing
// can only be faster. Callers that read r6/r7 afterwards see them unchanged.
// Proposed name: FlushDataCacheRange_821D11C8.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_FlushCache_821D11C8) {}

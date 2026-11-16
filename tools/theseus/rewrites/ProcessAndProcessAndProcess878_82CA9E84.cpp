// Readable rewrite of ProcessAndProcessAndProcess878_82CA9E84 (guest 0x82CA9E84,
// 9 PPC instructions, 21 callers).
// Gate: python3 tools/theseus/gate.py ProcessAndProcessAndProcess878_82CA9E84
//
// MSVC's stack probe (__chkstk): called with r12 = minus the size of a new stack
// frame, it reads one word in every 4 KB page below r1 so the console's stack
// guard page commits the stack in order. It writes nothing and returns nothing.
// In the recompiled game the guest stack is already mapped, so the reads have
// no effect: this rewrite does nothing (and is faster: the original reads one
// word per page). Callers read r4..r10 and r12 afterwards (they rely on
// __chkstk leaving them alone), which an empty function also does.
// Proposed name: ChkStk_82CA9E84.
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_ProcessAndProcessAndProcess878_82CA9E84) {}

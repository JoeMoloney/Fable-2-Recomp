// Readable rewrite of GetSubObjField4_821EEA78 (guest 0x821EEA78, 6 PPC
// instructions, 20 callers). Gate: python3 tools/theseus/gate.py GetSubObjField4_821EEA78
//
// r3 = object whose first word points at a sub-object (or is null). Returns the
// sub-object's word at +4, or 0 when there is no sub-object:
//   return obj->sub ? obj->sub->field4 : 0;
// Name kept (already GetSubObjField4).
#include "fable_2_pch.h"

extern "C" REX_FUNC(rewrite_GetSubObjField4_821EEA78) {
  const uint32_t sub = REX_LOAD_U32(ctx.r3.u32 + 0);
  ctx.r3.u64 = sub ? REX_LOAD_U32(sub + 4) : 0;
}

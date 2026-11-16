// Ship of Theseus pilot (plans/THESEUS_PILOT.md, unit T5): readable rewrites
// of recompiled functions, proven equal to the originals offline by
// tools/theseus/difftest.py, running in the game.
//
// Each override in this folder #includes the tested rewrite from
// tools/theseus/rewrites/<name>.cpp, so the game runs exactly the code the
// difftest checked. --theseus_rewrites=false falls back to the recompiled
// originals (__imp__<name>) for an A/B check without a rebuild.
#pragma once

#include "fable_2_pch.h"

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, theseus_rewrites);

#define THESEUS_OVERRIDE(name)                                             \
  extern "C" REX_FUNC(__imp__##name);                                      \
  extern "C" void name(PPCContext& __restrict ctx, uint8_t* base) {         \
    if (REXCVAR_GET(theseus_rewrites)) [[likely]]                          \
      rewrite_##name(ctx, base);                                           \
    else                                                                   \
      __imp__##name(ctx, base);                                            \
  }

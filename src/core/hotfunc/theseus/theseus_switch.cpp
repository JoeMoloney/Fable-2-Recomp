#include "theseus_switch.h"

REXCVAR_DEFINE_BOOL(theseus_rewrites, true, "Theseus",
                    "Run the verified readable rewrites in src/core/hotfunc/theseus/ "
                    "instead of the recompiled originals (plans/THESEUS_PILOT.md).");

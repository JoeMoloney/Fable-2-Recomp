// Theseus override of IndirectCall_8310FC68: runs the verified rewrite
// tools/theseus/rewrites/IndirectCall_8310FC68.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/gate.py IndirectCall_8310FC68
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/IndirectCall_8310FC68.cpp"

THESEUS_OVERRIDE(IndirectCall_8310FC68)

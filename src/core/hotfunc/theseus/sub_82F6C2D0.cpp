// Theseus override of sub_82F6C2D0: runs the verified rewrite
// tools/theseus/rewrites/sub_82F6C2D0.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py sub_82F6C2D0
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/sub_82F6C2D0.cpp"

THESEUS_OVERRIDE(sub_82F6C2D0)

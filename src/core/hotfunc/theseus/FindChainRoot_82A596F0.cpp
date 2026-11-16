// Theseus override of FindChainRoot_82A596F0: runs the verified rewrite
// tools/theseus/rewrites/FindChainRoot_82A596F0.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py FindChainRoot_82A596F0
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FindChainRoot_82A596F0.cpp"

THESEUS_OVERRIDE(FindChainRoot_82A596F0)

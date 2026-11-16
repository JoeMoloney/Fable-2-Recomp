// Theseus override of GrowAndZeroBuffer_82BC55B8: runs the verified rewrite
// tools/theseus/rewrites/GrowAndZeroBuffer_82BC55B8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py GrowAndZeroBuffer_82BC55B8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/GrowAndZeroBuffer_82BC55B8.cpp"

THESEUS_OVERRIDE(GrowAndZeroBuffer_82BC55B8)

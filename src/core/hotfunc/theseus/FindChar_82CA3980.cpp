// Theseus override of FindChar_82CA3980: runs the verified rewrite
// tools/theseus/rewrites/FindChar_82CA3980.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py FindChar_82CA3980
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/FindChar_82CA3980.cpp"

THESEUS_OVERRIDE(FindChar_82CA3980)

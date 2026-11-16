// Theseus override of StrCmpIgnoreCase_82CA6320: runs the verified rewrite
// tools/theseus/rewrites/StrCmpIgnoreCase_82CA6320.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py StrCmpIgnoreCase_82CA6320
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/StrCmpIgnoreCase_82CA6320.cpp"

THESEUS_OVERRIDE(StrCmpIgnoreCase_82CA6320)

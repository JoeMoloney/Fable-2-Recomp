// Theseus override of NormalizeVector_82260BC8: runs the verified rewrite
// tools/theseus/rewrites/NormalizeVector_82260BC8.cpp (see its header for what it is and
// how it was tested). Do not edit the logic here; edit the rewrite and rerun
//   python3 tools/theseus/difftest.py NormalizeVector_82260BC8
#include "theseus_switch.h"
#include "../../../../tools/theseus/rewrites/NormalizeVector_82260BC8.cpp"

THESEUS_OVERRIDE(NormalizeVector_82260BC8)
